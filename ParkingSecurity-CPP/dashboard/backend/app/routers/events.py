import base64
import logging
import os
import uuid
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends, HTTPException, Query, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.core.deps import get_current_user, verify_camera_api_key
from app.models import Camera, Event, User
from app.schemas.event import EventCreate, EventResponse
from app.services.alert_service import fire_all_alerts
from app.services.websocket_manager import ws_manager

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/events", tags=["events"])

SNAPSHOTS_BASE = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(__file__))), "snapshots")
if os.path.exists("/app/snapshots"):
    SNAPSHOTS_BASE = "/app/snapshots"


@router.post("", response_model=EventResponse, status_code=status.HTTP_201_CREATED)
async def create_event(
    body: EventCreate,
    db: AsyncSession = Depends(get_db),
    _: bool = Depends(verify_camera_api_key),
):
    # Save full frame if provided
    frame_path = None
    if body.frame_image:
        try:
            frame_data = base64.b64decode(body.frame_image)
            frame_filename = f"{uuid.uuid4()}.jpg"
            os.makedirs(os.path.join(SNAPSHOTS_BASE, "frames"), exist_ok=True)
            full_path = os.path.join(SNAPSHOTS_BASE, "frames", frame_filename)
            with open(full_path, "wb") as f:
                f.write(frame_data)
            frame_path = f"/snapshots/frames/{frame_filename}"
        except Exception as e:
            logger.error(f"Failed to save frame: {e}")

    event = Event(
        camera_id=body.camera_id,
        event_type=body.event_type,
        employee_id=body.employee_id if body.employee_id else None,
        employee_name=body.employee_name,
        confidence=body.confidence,
        bbox=body.bbox,
        face_image=body.face_image,
        frame_image=frame_path,
    )
    db.add(event)

    # Update camera last_seen
    result = await db.execute(select(Camera).where(Camera.id == body.camera_id))
    camera = result.scalar_one_or_none()
    if camera:
        camera.last_seen = datetime.now(timezone.utc)

    await db.commit()
    await db.refresh(event)

    # Broadcast via WebSocket
    ws_data = {
        "type": "detection",
        "event": {
            "id": str(event.id),
            "camera_id": event.camera_id,
            "event_type": event.event_type,
            "employee_name": event.employee_name,
            "confidence": event.confidence,
            "face_image": event.face_image,
            "frame_image": event.frame_image,
            "timestamp": event.timestamp.isoformat(),
        },
    }
    await ws_manager.broadcast(ws_data)

    # Fire alerts for unknown detections
    if event.event_type == "unknown":
        try:
            await fire_all_alerts(
                camera_id=event.camera_id,
                timestamp=event.timestamp.isoformat(),
                face_b64=event.face_image,
                event_id=str(event.id),
            )
            event.alert_sent = True
            await db.commit()
        except Exception as e:
            logger.error(f"Alert error: {e}")

    return event


@router.get("", response_model=list[EventResponse])
async def list_events(
    event_type: str | None = None,
    camera_id: str | None = None,
    hours: int = Query(24, ge=1, le=168),
    limit: int = Query(200, ge=1, le=1000),
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    since = datetime.now(timezone.utc) - timedelta(hours=hours)
    query = select(Event).where(Event.timestamp >= since)

    if event_type:
        query = query.where(Event.event_type == event_type)
    if camera_id:
        query = query.where(Event.camera_id == camera_id)

    query = query.order_by(Event.timestamp.desc()).limit(limit)
    result = await db.execute(query)
    return result.scalars().all()


@router.delete("/clear", status_code=status.HTTP_200_OK)
async def clear_events(
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    from sqlalchemy import delete
    result = await db.execute(delete(Event))
    await db.commit()
    return {"deleted": result.rowcount}
