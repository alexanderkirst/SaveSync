from __future__ import annotations

from datetime import datetime, timezone
from pydantic import BaseModel, Field, field_validator


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


class SaveMeta(BaseModel):
    game_id: str
    last_modified_utc: str = Field(default_factory=utc_now_iso)
    server_updated_at: str | None = None
    version: int = 0
    sha256: str
    size_bytes: int
    rom_sha1: str | None = None
    filename_hint: str | None = None
    platform_source: str | None = None
    conflict: bool = False

    @field_validator("game_id")
    @classmethod
    def validate_game_id(cls, value: str) -> str:
        v = value.strip()
        if not v:
            raise ValueError("game_id cannot be empty")
        return v

    @field_validator("rom_sha1")
    @classmethod
    def validate_rom_sha1(cls, value: str | None) -> str | None:
        if value is None:
            return None
        v = value.strip().lower()
        if not v:
            return None
        if len(v) != 40 or any(ch not in "0123456789abcdef" for ch in v):
            raise ValueError("rom_sha1 must be a 40-char hex SHA-1")
        return v


class SaveListItem(BaseModel):
    game_id: str
    last_modified_utc: str
    server_updated_at: str | None = None
    version: int = 0
    sha256: str
    size_bytes: int
    rom_sha1: str | None = None
    filename_hint: str | None = None
    platform_source: str | None = None
    conflict: bool = False


class SaveListResponse(BaseModel):
    saves: list[SaveListItem]
