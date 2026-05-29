from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends, Query
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.core.deps import require_admin
from app.models import AuditLog, User

router = APIRouter(prefix="/api/audit", tags=["audit"])


@router.get("", response_model=list[dict])
async def list_audit_logs(
    user_email: str | None = None,
    action: str | None = None,
    hours: int = Query(168, ge=1, le=720),
    limit: int = Query(200, ge=1, le=1000),
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    since = datetime.now(timezone.utc) - timedelta(hours=hours)
    query = select(AuditLog).where(AuditLog.timestamp >= since)

    if user_email:
        query = query.where(AuditLog.user_email == user_email)
    if action:
        query = query.where(AuditLog.action == action)

    query = query.order_by(AuditLog.timestamp.desc()).limit(limit)
    result = await db.execute(query)
    logs = result.scalars().all()

    return [
        {
            "id": str(log.id),
            "user_email": log.user_email,
            "action": log.action,
            "resource": log.resource,
            "resource_id": log.resource_id,
            "details": log.details,
            "ip_address": log.ip_address,
            "timestamp": log.timestamp.isoformat(),
        }
        for log in logs
    ]
