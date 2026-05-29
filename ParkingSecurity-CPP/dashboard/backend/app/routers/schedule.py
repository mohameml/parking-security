import io
import logging
import os
from datetime import date, datetime, time, timezone

from fastapi import APIRouter, Depends, File, HTTPException, Query, UploadFile
from sqlalchemy import select, and_
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.core.database import get_db
from app.core.deps import get_current_user, require_admin, verify_camera_api_key
from app.models import ExamSchedule, Student, StudentEmbedding, StudentExam, User

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/schedule", tags=["schedule"])

# Time slot definitions
TIME_SLOTS = {
    "H1": {"name": "Matin", "start": time(6, 0), "end": time(11, 0)},
    "H2": {"name": "Après-midi", "start": time(11, 30), "end": time(17, 0)},
}


@router.post("/import")
async def import_schedule(
    file: UploadFile = File(...),
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    """Import exam schedule + student assignments from Excel file."""
    import xlrd

    content = await file.read()
    wb = xlrd.open_workbook(file_contents=content)

    # ── Sheet 1: Affectations (exam sessions) ──
    sheet1 = wb.sheet_by_index(0)
    exams_created = 0

    for row_idx in range(1, sheet1.nrows):
        try:
            exam_id = int(sheet1.cell(row_idx, 0).value)
            room = str(sheet1.cell(row_idx, 1).value).strip()
            room_ar = str(sheet1.cell(row_idx, 2).value).strip()
            center = str(sheet1.cell(row_idx, 4).value).strip()
            time_slot_code = str(sheet1.cell(row_idx, 5).value).strip()
            subject_code = str(sheet1.cell(row_idx, 6).value).strip()
            subject = str(sheet1.cell(row_idx, 8).value).strip()
            program = str(sheet1.cell(row_idx, 11).value).strip()
            semester = str(sheet1.cell(row_idx, 12).value).strip()
            date_str = str(sheet1.cell(row_idx, 13).value).strip()

            # Parse date
            try:
                exam_date = datetime.strptime(date_str, "%d/%m/%Y").date()
            except ValueError:
                try:
                    # Excel date number
                    date_tuple = xlrd.xldate_as_tuple(sheet1.cell(row_idx, 13).value, wb.datemode)
                    exam_date = date(date_tuple[0], date_tuple[1], date_tuple[2])
                except Exception:
                    logger.warning(f"Skipping row {row_idx}: cannot parse date '{date_str}'")
                    continue

            slot = TIME_SLOTS.get(time_slot_code, TIME_SLOTS["H1"])

            # Upsert exam
            existing = await db.execute(select(ExamSchedule).where(ExamSchedule.id == exam_id))
            if existing.scalar_one_or_none():
                continue

            exam = ExamSchedule(
                id=exam_id,
                room=room,
                room_ar=room_ar,
                center=center,
                subject_code=subject_code,
                subject=subject,
                time_slot=time_slot_code,
                start_time=slot["start"],
                end_time=slot["end"],
                exam_date=exam_date,
                program=program,
                semester=semester,
            )
            db.add(exam)
            exams_created += 1
        except Exception as e:
            logger.warning(f"Skipping exam row {row_idx}: {e}")

    await db.commit()

    # ── Sheet 2: Affectation Etudiant (student assignments) ──
    sheet2 = wb.sheet_by_index(1)
    students_created = 0
    assignments_created = 0

    for row_idx in range(1, sheet2.nrows):
        try:
            student_id = str(sheet2.cell(row_idx, 1).value).strip()
            exam_id = int(sheet2.cell(row_idx, 2).value)
            seat = int(sheet2.cell(row_idx, 3).value) if sheet2.cell(row_idx, 3).value else None

            if not student_id:
                continue

            # Create student if not exists
            result = await db.execute(select(Student).where(Student.student_id == student_id))
            student = result.scalar_one_or_none()
            if not student:
                student = Student(student_id=student_id, is_active=True)
                db.add(student)
                await db.flush()
                students_created += 1

            # Create assignment if not exists
            result = await db.execute(
                select(StudentExam).where(
                    and_(StudentExam.student_id == student.id, StudentExam.exam_id == exam_id)
                )
            )
            if not result.scalar_one_or_none():
                assignment = StudentExam(
                    student_id=student.id,
                    exam_id=exam_id,
                    seat=seat,
                    presence=False,
                )
                db.add(assignment)
                assignments_created += 1

        except Exception as e:
            logger.warning(f"Skipping student row {row_idx}: {e}")

    await db.commit()

    return {
        "exams_created": exams_created,
        "students_created": students_created,
        "assignments_created": assignments_created,
    }


@router.post("/import-photos")
async def import_student_photos(
    photos_dir: str = "/app/student_photos",
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    """Extract face embeddings from student photos and store them.
    Photos should be named {NODOS}.png (e.g., B020785.png).
    Calls the camera service's InsightFace for embedding extraction.
    """
    import aiohttp
    import glob

    base_url = os.getenv("CAMERA_EMBEDDING_URL", "http://localhost:8080/extract-embedding")
    camera_url = base_url.replace("/extract-embedding", "/extract-embeddings-augmented")
    enrolled = 0
    failed = 0

    # Find all photos
    patterns = [os.path.join(photos_dir, "*.png"), os.path.join(photos_dir, "*.jpg")]
    photo_files = []
    for pattern in patterns:
        photo_files.extend(glob.glob(pattern))

    if not photo_files:
        raise HTTPException(status_code=400, detail=f"No photos found in {photos_dir}")

    async with aiohttp.ClientSession() as session:
        for photo_path in photo_files:
            filename = os.path.basename(photo_path)
            student_id = os.path.splitext(filename)[0]  # B020785.png -> B020785

            # Find student in DB
            result = await db.execute(select(Student).where(Student.student_id == student_id))
            student = result.scalar_one_or_none()
            if not student:
                continue

            # Skip if already has embedding
            result = await db.execute(
                select(StudentEmbedding).where(StudentEmbedding.student_id == student.id)
            )
            if result.scalar_one_or_none():
                continue

            # Extract embedding via camera service
            try:
                with open(photo_path, "rb") as f:
                    photo_bytes = f.read()

                async with session.post(
                    camera_url, data=photo_bytes,
                    headers={"Content-Type": "application/octet-stream"},
                    timeout=aiohttp.ClientTimeout(total=60),
                ) as resp:
                    if resp.status == 200:
                        data = await resp.json()
                        embeddings = data.get("embeddings", [])
                        if embeddings:
                            for emb_vec in embeddings:
                                emb = StudentEmbedding(
                                    student_id=student.id,
                                    embedding=emb_vec,
                                )
                                db.add(emb)
                            student.photo_path = f"/student_photos/{filename}"
                            enrolled += 1

                            if enrolled % 25 == 0:
                                await db.commit()
                                logger.info(f"Enrolled {enrolled} students with augmented embeddings...")
                        else:
                            failed += 1
                            logger.warning(f"No face in {filename}: {data.get('error')}")
                    else:
                        failed += 1
            except Exception as e:
                failed += 1
                logger.warning(f"Failed {filename}: {e}")

    await db.commit()
    logger.info(f"Photo import complete: {enrolled} enrolled, {failed} failed")
    return {"enrolled": enrolled, "failed": failed, "total_photos": len(photo_files)}


@router.get("/check/{student_id}")
async def check_student_schedule(
    student_id: str,
    db: AsyncSession = Depends(get_db),
):
    """Check if a student is authorized to be here right now.
    Called by the camera service after face identification.
    Returns: authorized | wrong_time | wrong_day | no_exam
    """
    now = datetime.now(timezone.utc)
    today = now.date()
    current_time = now.time()

    # Find student
    result = await db.execute(
        select(Student)
        .where(Student.student_id == student_id)
        .options(selectinload(Student.exams).selectinload(StudentExam.exam))
    )
    student = result.scalar_one_or_none()
    if not student:
        return {"status": "not_found", "student_id": student_id}

    # Get all exams for this student
    today_exams = []
    all_exam_dates = set()

    for se in student.exams:
        exam = se.exam
        if exam:
            all_exam_dates.add(exam.exam_date)
            if exam.exam_date == today:
                today_exams.append(exam)

    if not today_exams:
        # Student has no exam today
        has_any_exam = len(all_exam_dates) > 0
        return {
            "status": "wrong_day",
            "student_id": student_id,
            "message": "No exam scheduled for today",
            "next_exam_dates": sorted([str(d) for d in all_exam_dates if d > today])[:3],
        }

    # Check if current time falls within any of today's exam slots
    for exam in today_exams:
        if exam.start_time <= current_time <= exam.end_time:
            return {
                "status": "authorized",
                "student_id": student_id,
                "exam_id": exam.id,
                "subject": exam.subject,
                "room": exam.room,
                "time_slot": exam.time_slot,
                "message": f"Authorized for {exam.subject} in {exam.room}",
            }

    # Has exam today but wrong time slot
    return {
        "status": "wrong_time",
        "student_id": student_id,
        "today_exams": [
            {
                "subject": e.subject,
                "room": e.room,
                "time_slot": e.time_slot,
                "start": str(e.start_time),
                "end": str(e.end_time),
            }
            for e in today_exams
        ],
        "message": "Student has exam today but not at this time",
    }


@router.get("/today")
async def get_today_schedule(
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    """Get all exams scheduled for today."""
    today = datetime.now(timezone.utc).date()
    result = await db.execute(
        select(ExamSchedule)
        .where(ExamSchedule.exam_date == today)
        .order_by(ExamSchedule.time_slot, ExamSchedule.room)
    )
    exams = result.scalars().all()

    return [
        {
            "id": e.id,
            "room": e.room,
            "subject": e.subject,
            "time_slot": e.time_slot,
            "start_time": str(e.start_time),
            "end_time": str(e.end_time),
            "program": e.program,
            "semester": e.semester,
        }
        for e in exams
    ]


@router.get("/students")
async def list_students(
    search: str | None = None,
    page: int = Query(1, ge=1),
    page_size: int = Query(25, ge=1, le=200),
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    """List students paginated. Returns {items, total, page, page_size, pages}."""
    from sqlalchemy import func as sqlfunc

    base_query = select(Student)
    count_query = select(sqlfunc.count()).select_from(Student)

    if search:
        filter_expr = Student.student_id.ilike(f"%{search}%")
        base_query = base_query.where(filter_expr)
        count_query = count_query.where(filter_expr)

    total = (await db.execute(count_query)).scalar() or 0

    query = (
        base_query
        .options(
            selectinload(Student.embeddings),
            selectinload(Student.exams).selectinload(StudentExam.exam),
        )
        .order_by(Student.student_id)
        .offset((page - 1) * page_size)
        .limit(page_size)
    )
    result = await db.execute(query)
    students = result.scalars().all()

    items = [
        {
            "id": str(s.id),
            "type": "student",
            "employee_number": s.student_id,
            "full_name": s.student_id,
            "department": None,
            "email": None,
            "phone": None,
            "photo_path": s.photo_path,
            "is_active": s.is_active,
            "embedding_count": len(s.embeddings),
            "exam_count": len(s.exams),
            "next_exams": [
                {
                    "subject": se.exam.subject if se.exam else "",
                    "date": str(se.exam.exam_date) if se.exam else "",
                    "time_slot": se.exam.time_slot if se.exam else "",
                    "room": se.exam.room if se.exam else "",
                }
                for se in sorted(s.exams, key=lambda x: x.exam.exam_date if x.exam else date.min)[:3]
            ],
            "created_at": s.created_at.isoformat() if s.created_at else None,
        }
        for s in students
    ]

    pages = (total + page_size - 1) // page_size if total > 0 else 0
    return {
        "items": items,
        "total": total,
        "page": page,
        "page_size": page_size,
        "pages": pages,
    }


@router.post("/learn-embedding")
async def progressive_learning(
    payload: dict,
    db: AsyncSession = Depends(get_db),
    _: bool = Depends(verify_camera_api_key),
):
    """Progressive learning: add a high-confidence runtime embedding to the database.
    Called by the camera service when it confidently identifies a person (score > 0.70).
    This improves recognition over time by adding real-world-captured embeddings.
    """
    person_type = payload.get("person_type", "employee")
    person_id = payload.get("person_id")
    embedding = payload.get("embedding")

    if not person_id or not embedding or len(embedding) != 512:
        raise HTTPException(status_code=400, detail="Invalid payload")

    # Limit to avoid unbounded growth
    MAX_LEARNED_EMBEDDINGS = 20

    if person_type == "student":
        from app.models import StudentEmbedding
        # Count current embeddings
        count_result = await db.execute(
            select(StudentEmbedding).where(StudentEmbedding.student_id == person_id)
        )
        current_count = len(count_result.scalars().all())
        if current_count >= MAX_LEARNED_EMBEDDINGS:
            return {"added": False, "reason": "Max embeddings reached"}

        emb = StudentEmbedding(student_id=person_id, embedding=embedding)
        db.add(emb)
    else:
        from app.models import FaceEmbedding
        count_result = await db.execute(
            select(FaceEmbedding).where(FaceEmbedding.employee_id == person_id)
        )
        current_count = len(count_result.scalars().all())
        if current_count >= MAX_LEARNED_EMBEDDINGS:
            return {"added": False, "reason": "Max embeddings reached"}

        emb = FaceEmbedding(employee_id=person_id, embedding=embedding)
        db.add(emb)

    await db.commit()
    return {"added": True, "total": current_count + 1}


@router.get("/students/embeddings")
async def get_student_embeddings(
    db: AsyncSession = Depends(get_db),
):
    """Get all active student embeddings for the camera service."""
    result = await db.execute(
        select(StudentEmbedding)
        .join(Student)
        .where(Student.is_active == True)
        .options(selectinload(StudentEmbedding.student))
    )
    embeddings = result.scalars().all()

    return [
        {
            "student_id": emb.student.student_id,
            "employee_id": str(emb.student_id),  # compatible with camera service field name
            "employee_name": emb.student.student_id,  # display name
            "embedding": emb.embedding,
        }
        for emb in embeddings
    ]
