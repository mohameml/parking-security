import uuid
from datetime import datetime, timezone

from sqlalchemy import Boolean, Column, Date, DateTime, Float, ForeignKey, Integer, String, Text, Time, Index
from sqlalchemy.dialects.postgresql import ARRAY, UUID
from sqlalchemy.orm import relationship

from app.core.database import Base


def utcnow():
    return datetime.now(timezone.utc)


class User(Base):
    __tablename__ = "users"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    email = Column(String(255), unique=True, nullable=False, index=True)
    full_name = Column(String(255), nullable=False)
    hashed_password = Column(String(255), nullable=False)
    role = Column(String(20), nullable=False, default="guard")  # admin | guard
    is_active = Column(Boolean, default=True)
    failed_login_attempts = Column(Integer, default=0)
    locked_until = Column(DateTime(timezone=True), nullable=True)
    created_at = Column(DateTime(timezone=True), default=utcnow)
    updated_at = Column(DateTime(timezone=True), default=utcnow, onupdate=utcnow)


class Employee(Base):
    __tablename__ = "employees"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    employee_number = Column(String(50), unique=True, nullable=False, index=True)
    full_name = Column(String(255), nullable=False)
    department = Column(String(100), nullable=True)
    email = Column(String(255), nullable=True)
    phone = Column(String(50), nullable=True)
    photo_path = Column(String(500), nullable=True)
    is_active = Column(Boolean, default=True)
    created_at = Column(DateTime(timezone=True), default=utcnow)
    updated_at = Column(DateTime(timezone=True), default=utcnow, onupdate=utcnow)

    embeddings = relationship("FaceEmbedding", back_populates="employee", cascade="all, delete-orphan")


class FaceEmbedding(Base):
    __tablename__ = "face_embeddings"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    employee_id = Column(UUID(as_uuid=True), ForeignKey("employees.id", ondelete="CASCADE"), nullable=False)
    embedding = Column(ARRAY(Float), nullable=False)  # 512-dim vector
    created_at = Column(DateTime(timezone=True), default=utcnow)

    employee = relationship("Employee", back_populates="embeddings")


class Camera(Base):
    __tablename__ = "cameras"

    id = Column(String(100), primary_key=True)
    name = Column(String(255), nullable=False)
    location = Column(String(255), nullable=True)
    is_active = Column(Boolean, default=True)
    last_seen = Column(DateTime(timezone=True), nullable=True)
    created_at = Column(DateTime(timezone=True), default=utcnow)


class Event(Base):
    __tablename__ = "events"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    camera_id = Column(String(100), ForeignKey("cameras.id"), nullable=False)
    event_type = Column(String(20), nullable=False)  # authorized | wrong_time | wrong_day | unknown
    employee_id = Column(UUID(as_uuid=True), ForeignKey("employees.id"), nullable=True)
    employee_name = Column(String(255), nullable=True)
    confidence = Column(Float, nullable=True)
    bbox = Column(String(200), nullable=True)  # JSON string "[x1,y1,x2,y2]"
    face_image = Column(Text, nullable=True)  # base64 face crop
    frame_image = Column(String(500), nullable=True)  # path to full frame snapshot
    alert_sent = Column(Boolean, default=False)
    timestamp = Column(DateTime(timezone=True), default=utcnow, index=True)

    __table_args__ = (
        Index("ix_events_type_timestamp", "event_type", "timestamp"),
    )


class AuditLog(Base):
    __tablename__ = "audit_logs"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    user_id = Column(UUID(as_uuid=True), ForeignKey("users.id"), nullable=True)
    user_email = Column(String(255), nullable=True)
    action = Column(String(100), nullable=False)
    resource = Column(String(100), nullable=True)
    resource_id = Column(String(255), nullable=True)
    details = Column(Text, nullable=True)
    ip_address = Column(String(45), nullable=True)
    timestamp = Column(DateTime(timezone=True), default=utcnow)


# ─── Schedule Models ────────────────────────────────────────────────


class ExamSchedule(Base):
    __tablename__ = "exam_schedules"

    id = Column(Integer, primary_key=True)  # matches Excel "id" column
    room = Column(String(100), nullable=False)
    room_ar = Column(String(100), nullable=True)
    center = Column(String(200), nullable=True)
    subject_code = Column(String(50), nullable=True)
    subject = Column(String(300), nullable=False)
    time_slot = Column(String(10), nullable=False)  # H1 (Matin) | H2 (Après-midi)
    start_time = Column(Time, nullable=False)  # 06:00 or 11:30
    end_time = Column(Time, nullable=False)    # 11:00 or 17:00
    exam_date = Column(Date, nullable=False, index=True)
    program = Column(String(50), nullable=True)   # option_an_diplome
    semester = Column(String(10), nullable=True)   # S3, S4, etc.

    students = relationship("StudentExam", back_populates="exam", cascade="all, delete-orphan")

    __table_args__ = (
        Index("ix_exam_date_slot", "exam_date", "time_slot"),
    )


class Student(Base):
    __tablename__ = "students"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    student_id = Column(String(20), unique=True, nullable=False, index=True)  # NODOS (B020785)
    full_name = Column(String(255), nullable=True)
    photo_path = Column(String(500), nullable=True)
    is_active = Column(Boolean, default=True)
    created_at = Column(DateTime(timezone=True), default=utcnow)

    embeddings = relationship("StudentEmbedding", back_populates="student", cascade="all, delete-orphan")
    exams = relationship("StudentExam", back_populates="student", cascade="all, delete-orphan")


class StudentEmbedding(Base):
    __tablename__ = "student_embeddings"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    student_id = Column(UUID(as_uuid=True), ForeignKey("students.id", ondelete="CASCADE"), nullable=False)
    embedding = Column(ARRAY(Float), nullable=False)  # 512-dim vector
    created_at = Column(DateTime(timezone=True), default=utcnow)

    student = relationship("Student", back_populates="embeddings")


class StudentExam(Base):
    __tablename__ = "student_exams"

    id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    student_id = Column(UUID(as_uuid=True), ForeignKey("students.id", ondelete="CASCADE"), nullable=False)
    exam_id = Column(Integer, ForeignKey("exam_schedules.id", ondelete="CASCADE"), nullable=False)
    seat = Column(Integer, nullable=True)
    presence = Column(Boolean, default=False)

    student = relationship("Student", back_populates="exams")
    exam = relationship("ExamSchedule", back_populates="students")
