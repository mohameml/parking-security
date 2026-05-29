import base64
import io
import logging
import os
import uuid

from fastapi import APIRouter, Depends, File, Form, HTTPException, Request, UploadFile, status
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.orm import selectinload

from app.core.database import get_db
from app.core.deps import get_client_ip, get_current_user, require_admin
from app.models import AuditLog, Employee, FaceEmbedding, User
from app.schemas.employee import EmployeeCreate, EmployeeResponse, EmployeeUpdate, EmbeddingItem

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/employees", tags=["employees"])

SNAPSHOTS_BASE = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), "snapshots")
if os.path.exists("/app/snapshots"):
    SNAPSHOTS_BASE = "/app/snapshots"


async def extract_augmented_embeddings(photo_bytes: bytes) -> list[list[float]]:
    """Extract multiple augmented embeddings from a photo using TTA.
    Returns a list of 512-dim embedding vectors (up to 6 per photo).
    """
    import aiohttp

    base_url = os.getenv("CAMERA_EMBEDDING_URL", "http://localhost:8080/extract-embedding")
    camera_url = base_url.replace("/extract-embedding", "/extract-embeddings-augmented")

    try:
        async with aiohttp.ClientSession() as session:
            async with session.post(
                camera_url,
                data=photo_bytes,
                headers={"Content-Type": "application/octet-stream"},
                timeout=aiohttp.ClientTimeout(total=60),
            ) as resp:
                if resp.status == 200:
                    data = await resp.json()
                    embeddings = data.get("embeddings", [])
                    if embeddings:
                        logger.info(f"Augmented embeddings extracted: {len(embeddings)} variants")
                        return embeddings
                    else:
                        logger.warning(f"No face detected: {data.get('error', 'unknown')}")
                        return []
                else:
                    logger.error(f"Camera augmented embeddings returned {resp.status}")
                    return []
    except Exception as e:
        logger.error(f"Augmented embedding extraction error: {type(e).__name__}: {e}")
        return []


async def extract_embedding_from_photo(photo_bytes: bytes) -> list[float] | None:
    """Extract a 512-dim face embedding by calling the camera service's InsightFace."""
    import aiohttp

    # Camera service runs on the same host, port 8080
    camera_url = os.getenv("CAMERA_EMBEDDING_URL", "http://localhost:8080/extract-embedding")

    try:
        logger.info(f"Calling camera embedding service at {camera_url} with {len(photo_bytes)} bytes")
        async with aiohttp.ClientSession() as session:
            async with session.post(
                camera_url,
                data=photo_bytes,
                headers={"Content-Type": "application/octet-stream"},
                timeout=aiohttp.ClientTimeout(total=30),
            ) as resp:
                resp_text = await resp.text()
                logger.info(f"Camera service response: {resp.status} - {resp_text[:200]}")
                if resp.status == 200:
                    import json as _json
                    data = _json.loads(resp_text)
                    if data.get("embedding"):
                        logger.info(f"Embedding extracted, faces found: {data.get('faces_found', 1)}")
                        return data["embedding"]
                    else:
                        logger.warning(f"No face detected: {data.get('error', 'unknown')}")
                        return None
                else:
                    logger.error(f"Camera embedding service returned {resp.status}: {resp_text[:200]}")
                    return None
    except Exception as e:
        logger.error(f"Embedding extraction error (camera service): {type(e).__name__}: {e}")
        return None


@router.get("", response_model=list[EmployeeResponse])
async def list_employees(
    search: str | None = None,
    active_only: bool = False,
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    query = select(Employee).options(selectinload(Employee.embeddings))
    if active_only:
        query = query.where(Employee.is_active == True)
    if search:
        query = query.where(Employee.full_name.ilike(f"%{search}%"))
    query = query.order_by(Employee.full_name)

    result = await db.execute(query)
    employees = result.scalars().all()

    return [
        EmployeeResponse(
            id=emp.id,
            employee_number=emp.employee_number,
            full_name=emp.full_name,
            department=emp.department,
            email=emp.email,
            phone=emp.phone,
            photo_path=emp.photo_path,
            is_active=emp.is_active,
            embedding_count=len(emp.embeddings),
            created_at=emp.created_at,
            updated_at=emp.updated_at,
        )
        for emp in employees
    ]


@router.post("", response_model=EmployeeResponse, status_code=status.HTTP_201_CREATED)
async def create_employee(
    employee_number: str = Form(...),
    full_name: str = Form(...),
    department: str = Form(None),
    email: str = Form(None),
    phone: str = Form(None),
    photo: UploadFile | None = File(None),
    request: Request = None,
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    # Check duplicate employee_number
    existing = await db.execute(select(Employee).where(Employee.employee_number == employee_number))
    if existing.scalar_one_or_none():
        raise HTTPException(status_code=400, detail="Employee number already exists")

    emp = Employee(
        employee_number=employee_number,
        full_name=full_name,
        department=department,
        email=email,
        phone=phone,
        is_active=True,
    )

    embedding_count = 0
    if photo:
        photo_bytes = await photo.read()
        # Save photo
        photo_filename = f"{uuid.uuid4()}.jpg"
        photo_path = os.path.join(SNAPSHOTS_BASE, "employees", photo_filename)
        os.makedirs(os.path.join(SNAPSHOTS_BASE, "employees"), exist_ok=True)
        with open(photo_path, "wb") as f:
            f.write(photo_bytes)
        emp.photo_path = f"/snapshots/employees/{photo_filename}"

        # Extract multiple embeddings via TTA (flip, rotations, brightness variations)
        embeddings = await extract_augmented_embeddings(photo_bytes)
        if embeddings:
            for emb in embeddings:
                face_emb = FaceEmbedding(employee_id=emp.id, embedding=emb)
                emp.embeddings.append(face_emb)
            embedding_count = len(embeddings)
            logger.info(f"Enrolled employee {full_name} with {embedding_count} augmented embeddings")
        else:
            logger.warning(f"No face detected in photo for employee {full_name}")

    db.add(emp)

    ip = get_client_ip(request) if request else "unknown"
    audit = AuditLog(
        user_id=admin.id, user_email=admin.email,
        action="create_employee", resource="employee",
        resource_id=str(emp.id), details=f"Created employee {full_name}",
        ip_address=ip,
    )
    db.add(audit)
    await db.commit()
    await db.refresh(emp)

    return EmployeeResponse(
        id=emp.id,
        employee_number=emp.employee_number,
        full_name=emp.full_name,
        department=emp.department,
        email=emp.email,
        phone=emp.phone,
        photo_path=emp.photo_path,
        is_active=emp.is_active,
        embedding_count=embedding_count,
        created_at=emp.created_at,
        updated_at=emp.updated_at,
    )


@router.put("/{employee_id}", response_model=EmployeeResponse)
async def update_employee(
    employee_id: str,
    full_name: str = Form(None),
    department: str = Form(None),
    email: str = Form(None),
    phone: str = Form(None),
    is_active: str = Form(None),
    photo: UploadFile | None = File(None),
    request: Request = None,
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    result = await db.execute(
        select(Employee).options(selectinload(Employee.embeddings)).where(Employee.id == employee_id)
    )
    emp = result.scalar_one_or_none()
    if not emp:
        raise HTTPException(status_code=404, detail="Employee not found")

    if full_name is not None:
        emp.full_name = full_name
    if department is not None:
        emp.department = department
    if email is not None:
        emp.email = email
    if phone is not None:
        emp.phone = phone
    if is_active is not None:
        emp.is_active = is_active.lower() in ("true", "1", "yes")

    if photo:
        photo_bytes = await photo.read()
        photo_filename = f"{uuid.uuid4()}.jpg"
        import os
        os.makedirs("/app/snapshots/employees", exist_ok=True)
        photo_path = f"/app/snapshots/employees/{photo_filename}"
        with open(photo_path, "wb") as f:
            f.write(photo_bytes)
        emp.photo_path = f"/snapshots/employees/{photo_filename}"

        # Remove old embeddings and re-enroll
        for old_emb in emp.embeddings:
            await db.delete(old_emb)

        embeddings = await extract_augmented_embeddings(photo_bytes)
        for emb in embeddings:
            face_emb = FaceEmbedding(employee_id=emp.id, embedding=emb)
            db.add(face_emb)

    ip = get_client_ip(request) if request else "unknown"
    audit = AuditLog(
        user_id=admin.id, user_email=admin.email,
        action="update_employee", resource="employee",
        resource_id=str(emp.id), details=f"Updated employee {emp.full_name}",
        ip_address=ip,
    )
    db.add(audit)
    await db.commit()
    await db.refresh(emp, attribute_names=["embeddings"])

    return EmployeeResponse(
        id=emp.id,
        employee_number=emp.employee_number,
        full_name=emp.full_name,
        department=emp.department,
        email=emp.email,
        phone=emp.phone,
        photo_path=emp.photo_path,
        is_active=emp.is_active,
        embedding_count=len(emp.embeddings),
        created_at=emp.created_at,
        updated_at=emp.updated_at,
    )


@router.delete("/{employee_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_employee(
    employee_id: str,
    request: Request,
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    result = await db.execute(select(Employee).where(Employee.id == employee_id))
    emp = result.scalar_one_or_none()
    if not emp:
        raise HTTPException(status_code=404, detail="Employee not found")

    ip = get_client_ip(request)
    audit = AuditLog(
        user_id=admin.id, user_email=admin.email,
        action="delete_employee", resource="employee",
        resource_id=str(emp.id), details=f"Deleted employee {emp.full_name}",
        ip_address=ip,
    )
    db.add(audit)
    await db.delete(emp)
    await db.commit()


@router.get("/embeddings", response_model=list[EmbeddingItem])
async def get_embeddings(db: AsyncSession = Depends(get_db)):
    """Called by camera service to get all active employee embeddings."""
    result = await db.execute(
        select(FaceEmbedding)
        .join(Employee)
        .where(Employee.is_active == True)
        .options(selectinload(FaceEmbedding.employee))
    )
    embeddings = result.scalars().all()

    return [
        EmbeddingItem(
            employee_id=str(emb.employee_id),
            employee_name=emb.employee.full_name,
            embedding=emb.embedding,
        )
        for emb in embeddings
    ]
