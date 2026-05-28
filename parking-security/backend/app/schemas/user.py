from datetime import datetime
from uuid import UUID

from pydantic import BaseModel


class UserCreate(BaseModel):
    email: str
    full_name: str
    password: str
    role: str = "guard"


class UserUpdate(BaseModel):
    full_name: str | None = None
    is_active: bool | None = None
    role: str | None = None


class UserResponse(BaseModel):
    id: UUID
    email: str
    full_name: str
    role: str
    is_active: bool
    created_at: datetime

    class Config:
        from_attributes = True
