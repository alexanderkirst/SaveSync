"""Browser admin UI API (session cookie + optional X-API-Key). Static assets live in admin-web/static."""

from __future__ import annotations

import hashlib
import hmac
import json
import logging
import os
import secrets
from pathlib import Path
from typing import Annotated, Any

from fastapi import APIRouter, Depends, HTTPException, Request, status
from fastapi.responses import JSONResponse, RedirectResponse
from pydantic import BaseModel, Field

_log = logging.getLogger("gbasync.admin")

router = APIRouter(prefix="/admin", tags=["admin"])


def _env(name: str, default: str = "") -> str:
    if name.startswith("GBASYNC_"):
        legacy = "SAVESYNC_" + name[len("GBASYNC_") :]
        v = os.getenv(name, "").strip()
        if v:
            return v
        return os.getenv(legacy, default).strip()
    return os.getenv(name, default).strip()


def _admin_password_configured() -> bool:
    return bool(_env("GBASYNC_ADMIN_PASSWORD"))


def _admin_session_token() -> str:
    """Deterministic cookie value derived from password + secret (no server-side session store)."""
    secret = _env("GBASYNC_ADMIN_SECRET") or _env("API_KEY")
    pw = _env("GBASYNC_ADMIN_PASSWORD")
    if not secret or not pw:
        return ""
    return hmac.new(secret.encode("utf-8"), pw.encode("utf-8"), hashlib.sha256).hexdigest()


def _require_admin_enabled() -> None:
    if not _admin_password_configured():
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="admin UI disabled (set GBASYNC_ADMIN_PASSWORD)")


def require_admin(request: Request) -> bool:
    _require_admin_enabled()
    token = _admin_session_token()
    if not token:
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="admin misconfigured")
    cookie = request.cookies.get("gbasync_admin_session")
    if cookie and secrets.compare_digest(cookie, token):
        return True
    api = (request.headers.get("X-API-Key") or "").strip()
    api_expected = _env("API_KEY")
    if api_expected and secrets.compare_digest(api, api_expected.strip()):
        return True
    raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="unauthorized")


AdminDep = Annotated[bool, Depends(require_admin)]


class LoginBody(BaseModel):
    password: str = Field(..., min_length=1)


def _get_store(request: Request) -> SaveStore:
    # Late import to avoid circular import at module load
    from . import main as main_mod

    return main_mod.store


@router.get("", include_in_schema=False)
def admin_root_redirect() -> RedirectResponse:
    return RedirectResponse(url="/admin/ui/", status_code=302)


@router.get("/api/me")
def admin_me(request: Request) -> dict[str, Any]:
    if not _admin_password_configured():
        return {"admin_enabled": False, "authenticated": False}
    token = _admin_session_token()
    if not token:
        return {"admin_enabled": True, "authenticated": False, "misconfigured": True}
    cookie = request.cookies.get("gbasync_admin_session")
    authed = bool(cookie and secrets.compare_digest(cookie, token))
    if not authed:
        api = (request.headers.get("X-API-Key") or "").strip()
        api_expected = _env("API_KEY")
        if api_expected and secrets.compare_digest(api, api_expected.strip()):
            authed = True
    return {"admin_enabled": True, "authenticated": authed, "misconfigured": False}


@router.post("/api/login")
def admin_login(body: LoginBody) -> JSONResponse:
    _require_admin_enabled()
    token = _admin_session_token()
    if not token:
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="admin misconfigured")
    expected_pw = _env("GBASYNC_ADMIN_PASSWORD")
    if not secrets.compare_digest(
        hashlib.sha256(body.password.encode("utf-8")).hexdigest(),
        hashlib.sha256(expected_pw.encode("utf-8")).hexdigest(),
    ):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid password")
    response = JSONResponse(status_code=200, content={"ok": True})
    response.set_cookie(
        key="gbasync_admin_session",
        value=token,
        httponly=True,
        samesite="lax",
        max_age=86400 * 30,
        path="/admin",
    )
    return response


@router.post("/api/logout")
def admin_logout() -> JSONResponse:
    r = JSONResponse(status_code=200, content={"ok": True})
    r.delete_cookie("gbasync_admin_session", path="/admin")
    return r


@router.get("/api/dashboard")
def admin_dashboard(_: AdminDep, request: Request) -> dict[str, Any]:
    store = _get_store(request)
    saves = store.list_saves()
    conflicts = store.list_conflicts()
    mode = _env("GBASYNC_DROPBOX_MODE", "off").lower()
    return {
        "dropbox_mode": mode,
        "save_count": len(saves),
        "conflict_count": len(conflicts),
        "index_path": str(store.index_path),
        "save_root": str(store.save_root),
        "history_root": str(store.history_root),
    }


@router.get("/api/index-state")
def admin_index_state(_: AdminDep, request: Request) -> dict[str, Any]:
    store = _get_store(request)
    routing = store.export_index_routing()
    return {
        "aliases": routing["aliases"],
        "rom_sha1": routing["rom_sha1"],
        "tombstones": routing["tombstones"],
    }


@router.get("/api/slot-map")
def admin_slot_map(_: AdminDep) -> dict[str, Any]:
    raw = _env("GBASYNC_SLOT_MAP_PATH") or _env("SAVESYNC_SLOT_MAP_PATH")
    if not raw:
        return {"configured": False, "path": None, "json": None}
    path = Path(raw).expanduser()
    if not path.is_file():
        return {"configured": True, "path": str(path), "json": None, "error": "file not found"}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        return {"configured": True, "path": str(path), "json": None, "error": str(exc)}
    return {"configured": True, "path": str(path), "json": data}


@router.get("/api/saves")
def admin_list_saves(_: AdminDep, request: Request) -> dict[str, Any]:
    from .models import SaveListResponse

    store = _get_store(request)
    return SaveListResponse(saves=store.list_saves()).model_dump()


@router.get("/api/conflicts")
def admin_list_conflicts(_: AdminDep, request: Request) -> dict[str, Any]:
    from .models import SaveListResponse

    store = _get_store(request)
    return SaveListResponse(saves=store.list_conflicts()).model_dump()


@router.post("/api/dropbox/sync-once")
def admin_dropbox_sync_once(_: AdminDep) -> JSONResponse:
    mode = _env("GBASYNC_DROPBOX_MODE", "off").lower()
    if mode not in ("delta_api", "plain"):
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="GBASYNC_DROPBOX_MODE must be delta_api or plain",
        )
    from . import main as main_mod

    main_mod._run_dropbox_bridge_once()
    return JSONResponse(status_code=200, content={"ok": True, "mode": mode})


@router.post("/api/resolve/{game_id}")
def admin_resolve(_: AdminDep, request: Request, game_id: str) -> JSONResponse:
    store = _get_store(request)
    if not store.resolve_conflict(game_id):
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Save not found")
    _log.info("admin resolve_conflict game_id=%s", game_id)
    return JSONResponse(status_code=200, content={"game_id": game_id, "resolved": True})


@router.delete("/api/save/{game_id}")
def admin_delete_save(_: AdminDep, request: Request, game_id: str) -> JSONResponse:
    if not game_id or "/" in game_id or "\\" in game_id or game_id.startswith("."):
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="invalid game_id")
    store = _get_store(request)
    if not store.remove(game_id):
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Save not in index")
    _log.info("admin delete_save game_id=%s", game_id)
    return JSONResponse(status_code=200, content={"game_id": game_id, "deleted": True})
