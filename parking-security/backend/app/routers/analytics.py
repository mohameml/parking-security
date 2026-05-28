from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends, Query
from sqlalchemy import case, cast, func, Date, Integer, select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.core.deps import get_current_user
from app.models import Event, User

router = APIRouter(prefix="/api/analytics", tags=["analytics"])


@router.get("/summary")
async def analytics_summary(
    days: int = Query(7, ge=1, le=90),
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    since = datetime.now(timezone.utc) - timedelta(days=days)

    total_q = select(func.count()).select_from(Event).where(Event.timestamp >= since)
    known_q = total_q.where(Event.event_type == "known")
    unknown_q = total_q.where(Event.event_type == "unknown")

    total = (await db.execute(select(func.count()).select_from(Event).where(Event.timestamp >= since))).scalar() or 0
    known = (await db.execute(select(func.count()).select_from(Event).where(Event.timestamp >= since, Event.event_type == "known"))).scalar() or 0
    unknown = (await db.execute(select(func.count()).select_from(Event).where(Event.timestamp >= since, Event.event_type == "unknown"))).scalar() or 0

    return {
        "total": total,
        "known": known,
        "unknown": unknown,
        "unknown_rate": round(unknown / total * 100, 1) if total > 0 else 0,
        "days": days,
    }


@router.get("/daily")
async def analytics_daily(
    days: int = Query(7, ge=1, le=90),
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    since = datetime.now(timezone.utc) - timedelta(days=days)

    query = (
        select(
            cast(Event.timestamp, Date).label("date"),
            func.count().filter(Event.event_type == "known").label("known"),
            func.count().filter(Event.event_type == "unknown").label("unknown"),
        )
        .where(Event.timestamp >= since)
        .group_by(cast(Event.timestamp, Date))
        .order_by(cast(Event.timestamp, Date))
    )

    result = await db.execute(query)
    rows = result.all()

    return [
        {"date": str(row.date), "known": row.known, "unknown": row.unknown}
        for row in rows
    ]


@router.get("/hourly")
async def analytics_hourly(
    days: int = Query(7, ge=1, le=90),
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    since = datetime.now(timezone.utc) - timedelta(days=days)

    query = (
        select(
            func.extract("hour", Event.timestamp).label("hour"),
            func.count().label("count"),
        )
        .where(Event.timestamp >= since)
        .group_by(func.extract("hour", Event.timestamp))
        .order_by(func.extract("hour", Event.timestamp))
    )

    result = await db.execute(query)
    rows = result.all()

    return [{"hour": int(row.hour), "count": row.count} for row in rows]


@router.get("/top-employees")
async def analytics_top_employees(
    days: int = Query(7, ge=1, le=90),
    limit: int = Query(10, ge=1, le=50),
    db: AsyncSession = Depends(get_db),
    user: User = Depends(get_current_user),
):
    since = datetime.now(timezone.utc) - timedelta(days=days)

    query = (
        select(
            Event.employee_name,
            Event.employee_id,
            func.count().label("visits"),
        )
        .where(Event.timestamp >= since, Event.event_type == "known", Event.employee_name.isnot(None))
        .group_by(Event.employee_name, Event.employee_id)
        .order_by(func.count().desc())
        .limit(limit)
    )

    result = await db.execute(query)
    rows = result.all()

    return [
        {"employee_name": row.employee_name, "employee_id": str(row.employee_id), "visits": row.visits}
        for row in rows
    ]
