from fastapi import APIRouter, Depends, HTTPException, Request, status
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.core.deps import get_client_ip, require_admin
from app.core.security import hash_password
from app.models import AuditLog, User
from app.schemas.user import UserCreate, UserResponse, UserUpdate

router = APIRouter(prefix="/api/users", tags=["users"])


@router.get("", response_model=list[UserResponse])
async def list_users(
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    result = await db.execute(select(User).order_by(User.created_at.desc()))
    return result.scalars().all()


@router.post("", response_model=UserResponse, status_code=status.HTTP_201_CREATED)
async def create_user(
    body: UserCreate,
    request: Request,
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    existing = await db.execute(select(User).where(User.email == body.email))
    if existing.scalar_one_or_none():
        raise HTTPException(status_code=400, detail="Email already exists")

    user = User(
        email=body.email,
        full_name=body.full_name,
        hashed_password=hash_password(body.password),
        role=body.role if body.role in ("admin", "guard") else "guard",
        is_active=True,
    )
    db.add(user)

    ip = get_client_ip(request)
    audit = AuditLog(
        user_id=admin.id, user_email=admin.email,
        action="create_user", resource="user",
        resource_id=str(user.id), details=f"Created user {body.email} with role {body.role}",
        ip_address=ip,
    )
    db.add(audit)
    await db.commit()
    await db.refresh(user)
    return user


@router.put("/{user_id}", response_model=UserResponse)
async def update_user(
    user_id: str,
    body: UserUpdate,
    request: Request,
    db: AsyncSession = Depends(get_db),
    admin: User = Depends(require_admin),
):
    result = await db.execute(select(User).where(User.id == user_id))
    user = result.scalar_one_or_none()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    if body.full_name is not None:
        user.full_name = body.full_name
    if body.role is not None and body.role in ("admin", "guard"):
        user.role = body.role
    if body.is_active is not None:
        if str(user.id) == str(admin.id) and not body.is_active:
            raise HTTPException(status_code=400, detail="Cannot deactivate yourself")
        user.is_active = body.is_active

    ip = get_client_ip(request)
    audit = AuditLog(
        user_id=admin.id, user_email=admin.email,
        action="update_user", resource="user",
        resource_id=str(user.id), details=f"Updated user {user.email}",
        ip_address=ip,
    )
    db.add(audit)
    await db.commit()
    await db.refresh(user)
    return user
