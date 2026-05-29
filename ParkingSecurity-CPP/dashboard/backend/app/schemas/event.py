from datetime import datetime
from uuid import UUID

from pydantic import BaseModel


class EventCreate(BaseModel):
    camera_id: str
    event_type: str  # known | unknown
    employee_id: str | None = None
    employee_name: str | None = None
    confidence: float | None = None
    bbox: str | None = None
    face_image: str | None = None  # base64
    frame_image: str | None = None  # base64 full frame


class EventResponse(BaseModel):
    id: UUID
    camera_id: str
    event_type: str
    employee_id: UUID | None
    employee_name: str | None
    confidence: float | None
    bbox: str | None
    face_image: str | None
    frame_image: str | None
    alert_sent: bool
    timestamp: datetime

    class Config:
        from_attributes = True
