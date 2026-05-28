from datetime import datetime
from uuid import UUID

from pydantic import BaseModel


class EmployeeCreate(BaseModel):
    employee_number: str
    full_name: str
    department: str | None = None
    email: str | None = None
    phone: str | None = None


class EmployeeUpdate(BaseModel):
    full_name: str | None = None
    department: str | None = None
    email: str | None = None
    phone: str | None = None
    is_active: bool | None = None


class EmployeeResponse(BaseModel):
    id: UUID
    employee_number: str
    full_name: str
    department: str | None
    email: str | None
    phone: str | None
    photo_path: str | None
    is_active: bool
    embedding_count: int = 0
    created_at: datetime
    updated_at: datetime

    class Config:
        from_attributes = True


class EmbeddingItem(BaseModel):
    employee_id: str
    employee_name: str
    embedding: list[float]
