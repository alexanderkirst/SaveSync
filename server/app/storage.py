from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from threading import Lock

from .models import SaveMeta, SaveListItem


def parse_utc(iso_value: str) -> datetime:
    return datetime.fromisoformat(iso_value.replace("Z", "+00:00")).astimezone(timezone.utc)


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


@dataclass
class _IndexState:
    saves: dict[str, dict]
    aliases: dict[str, str]
    rom_sha1: dict[str, str]
    tombstones: dict[str, str]


class SaveStore:
    def __init__(self, save_root: Path, history_root: Path, index_path: Path, keep_history: bool = True):
        self.save_root = save_root
        self.history_root = history_root
        self.index_path = index_path
        self.keep_history = keep_history
        self._lock = Lock()
        self.save_root.mkdir(parents=True, exist_ok=True)
        self.history_root.mkdir(parents=True, exist_ok=True)
        self.index_path.parent.mkdir(parents=True, exist_ok=True)
        self._ensure_valid_index_file()

    def _ensure_valid_index_file(self) -> None:
        """Create or repair index.json (empty/truncated/invalid files break JSON reads and uploads)."""
        if not self.index_path.is_file():
            self._write_index_state(_IndexState(saves={}, aliases={}, rom_sha1={}, tombstones={}))
            return
        try:
            raw = self.index_path.read_text(encoding="utf-8")
            if not raw.strip():
                self._write_index_state(_IndexState(saves={}, aliases={}, rom_sha1={}, tombstones={}))
                return
            data = json.loads(raw)
            if not isinstance(data, dict):
                self._write_index_state(_IndexState(saves={}, aliases={}, rom_sha1={}, tombstones={}))
        except (json.JSONDecodeError, OSError, UnicodeError):
            self._write_index_state(_IndexState(saves={}, aliases={}, rom_sha1={}, tombstones={}))

    def _read_index_state(self) -> _IndexState:
        try:
            if not self.index_path.is_file() or self.index_path.stat().st_size == 0:
                return _IndexState(saves={}, aliases={}, rom_sha1={}, tombstones={})
            with self.index_path.open("r", encoding="utf-8") as fh:
                data = json.load(fh)
            if not isinstance(data, dict):
                return _IndexState(saves={}, aliases={}, rom_sha1={}, tombstones={})
            if isinstance(data.get("saves"), dict):
                return _IndexState(
                    saves={str(k): v for k, v in data["saves"].items() if isinstance(v, dict)},
                    aliases={
                        str(k).strip().lower(): str(v).strip().lower()
                        for k, v in (data.get("aliases") or {}).items()
                        if str(k).strip() and str(v).strip()
                    },
                    rom_sha1={
                        str(k).strip().lower(): str(v).strip().lower()
                        for k, v in (data.get("rom_sha1") or {}).items()
                        if str(k).strip() and str(v).strip()
                    },
                    tombstones={
                        str(k).strip().lower(): str(v).strip().lower()
                        for k, v in (data.get("tombstones") or {}).items()
                        if str(k).strip() and str(v).strip()
                    },
                )
            # Legacy format: top-level {game_id: meta}
            legacy = {str(k): v for k, v in data.items() if isinstance(v, dict)}
            return _IndexState(saves=legacy, aliases={}, rom_sha1={}, tombstones={})
        except (json.JSONDecodeError, OSError, UnicodeError):
            return _IndexState(saves={}, aliases={}, rom_sha1={}, tombstones={})

    def _write_index_state(self, state: _IndexState) -> None:
        tmp = self.index_path.with_suffix(".tmp")
        payload: dict[str, object]
        if state.aliases or state.rom_sha1 or state.tombstones:
            payload = {
                "saves": state.saves,
                "aliases": state.aliases,
                "rom_sha1": state.rom_sha1,
                "tombstones": state.tombstones,
            }
        else:
            payload = state.saves
        with tmp.open("w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2, sort_keys=True)
        os.replace(tmp, self.index_path)

    @staticmethod
    def _normalize_game_id(game_id: str) -> str:
        return game_id.strip().lower()

    @staticmethod
    def _filename_alias(filename_hint: str | None) -> str | None:
        if not filename_hint:
            return None
        stem = Path(filename_hint).stem.strip().lower()
        if not stem:
            return None
        slug = re.sub(r"[^a-z0-9._-]+", "-", stem).strip("-")
        return slug or None

    def _resolve_canonical_game_id(
        self,
        state: _IndexState,
        game_id: str,
        *,
        filename_hint: str | None = None,
        rom_sha1: str | None = None,
    ) -> str:
        gid = self._normalize_game_id(game_id)
        if rom_sha1:
            mapped = state.rom_sha1.get(rom_sha1.strip().lower())
            if mapped:
                return mapped
        mapped = state.aliases.get(gid)
        if mapped:
            return mapped
        if gid in state.saves:
            return gid
        filename_alias = self._filename_alias(filename_hint)
        if filename_alias:
            mapped = state.aliases.get(filename_alias)
            if mapped:
                return mapped
        return state.tombstones.get(gid, gid)

    @staticmethod
    def _register_alias(state: _IndexState, alias: str | None, canonical: str) -> None:
        if not alias:
            return
        a = alias.strip().lower()
        c = canonical.strip().lower()
        if not a or not c:
            return
        prev = state.aliases.get(a)
        if prev and prev != c and prev in state.saves:
            return
        state.aliases[a] = c

    def _register_identity_maps(
        self,
        state: _IndexState,
        *,
        canonical: str,
        upload_game_id: str,
        filename_hint: str | None,
        rom_sha1: str | None,
    ) -> None:
        self._register_alias(state, upload_game_id, canonical)
        self._register_alias(state, self._filename_alias(filename_hint), canonical)
        if rom_sha1:
            state.rom_sha1[rom_sha1.strip().lower()] = canonical

    def _merge_duplicate_saves(self, state: _IndexState) -> bool:
        changed = False
        by_sha: dict[str, list[str]] = {}
        for gid, raw in state.saves.items():
            digest = str(raw.get("sha256", "") or "").strip().lower()
            if digest:
                by_sha.setdefault(digest, []).append(gid)

        def alias_count(cid: str) -> int:
            return sum(1 for _a, target in state.aliases.items() if target == cid)

        def rom_count(cid: str) -> int:
            return sum(1 for _sha, target in state.rom_sha1.items() if target == cid)

        for _digest, ids in by_sha.items():
            if len(ids) < 2:
                continue
            winner = sorted(
                ids,
                key=lambda cid: (-rom_count(cid), -alias_count(cid), -int(state.saves[cid].get("version", 0)), cid),
            )[0]
            winner_raw = state.saves.get(winner)
            if not winner_raw:
                continue
            winner_hint = self._filename_alias(str(winner_raw.get("filename_hint") or ""))
            merged_any = False
            for loser in ids:
                if loser == winner:
                    continue
                loser_raw = state.saves.get(loser)
                if not loser_raw:
                    continue
                loser_hint = self._filename_alias(str(loser_raw.get("filename_hint") or ""))
                winner_tokens = {winner, winner.replace("-", "")}
                loser_tokens = {loser, loser.replace("-", "")}
                if winner_hint:
                    winner_tokens.add(winner_hint)
                if loser_hint:
                    loser_tokens.add(loser_hint)
                overlap = bool(winner_tokens & loser_tokens)
                if not overlap:
                    continue
                state.tombstones[loser] = winner
                state.aliases[loser] = winner
                for alias, target in list(state.aliases.items()):
                    if target == loser:
                        state.aliases[alias] = winner
                for rsha, target in list(state.rom_sha1.items()):
                    if target == loser:
                        state.rom_sha1[rsha] = winner
                del state.saves[loser]
                loser_path = self.save_path(loser)
                winner_path = self.save_path(winner)
                if loser_path.is_file() and not winner_path.is_file():
                    loser_path.replace(winner_path)
                elif loser_path.is_file():
                    loser_path.unlink()
                changed = True
                merged_any = True
            if merged_any:
                self._register_alias(state, winner_hint, winner)
        return changed

    def save_path(self, game_id: str) -> Path:
        return self.save_root / f"{game_id}.sav"

    def history_dir(self, game_id: str) -> Path:
        path = self.history_root / game_id
        path.mkdir(parents=True, exist_ok=True)
        return path

    def list_saves(self) -> list[SaveListItem]:
        with self._lock:
            state = self._read_index_state()
        out: list[SaveListItem] = []
        for game_id, raw in state.saves.items():
            # Index rows can outlive the blob (manual delete, restore mishap). Omit from /saves so
            # clients do not try GET /save/{id} and hit 404 during auto sync.
            if not self.save_path(game_id).is_file():
                continue
            out.append(SaveListItem(game_id=game_id, **raw))
        out.sort(key=lambda item: item.game_id)
        return out

    def list_conflicts(self) -> list[SaveListItem]:
        return [item for item in self.list_saves() if item.conflict]

    def export_index_routing(self) -> dict[str, dict[str, str]]:
        """Read-only aliases / rom_sha1 / tombstones for admin UI."""
        with self._lock:
            state = self._read_index_state()
        return {
            "aliases": dict(state.aliases),
            "rom_sha1": dict(state.rom_sha1),
            "tombstones": dict(state.tombstones),
        }

    def get_meta(self, game_id: str) -> SaveMeta | None:
        with self._lock:
            state = self._read_index_state()
        canonical = self._resolve_canonical_game_id(state, game_id)
        raw = state.saves.get(canonical)
        if not raw:
            return None
        return SaveMeta(game_id=canonical, **raw)

    def get_bytes(self, game_id: str) -> bytes | None:
        with self._lock:
            state = self._read_index_state()
            canonical = self._resolve_canonical_game_id(state, game_id)
        path = self.save_path(canonical)
        if not path.exists():
            return None
        return path.read_bytes()

    def _backup_existing(self, game_id: str, existing_meta: SaveMeta | None) -> None:
        if not self.keep_history:
            return
        existing_file = self.save_path(game_id)
        if not existing_file.exists() or not existing_meta:
            return
        stamp_src = existing_meta.server_updated_at or existing_meta.last_modified_utc
        stamp = stamp_src.replace(":", "-")
        history_path = self.history_dir(game_id) / f"{stamp}-{existing_meta.sha256[:8]}.sav"
        shutil.copy2(existing_file, history_path)

    def upsert(self, game_id: str, data: bytes, incoming: SaveMeta, force: bool = False) -> tuple[SaveMeta, bool, bool, str]:
        """
        Returns: (effective_meta, conflict_detected, applied_to_disk, canonical_game_id)

        ``applied_to_disk`` is False when the incoming payload was not written (no-op).
        """
        computed = hashlib.sha256(data).hexdigest()
        if computed != incoming.sha256:
            raise ValueError("sha256 mismatch")

        with self._lock:
            state = self._read_index_state()
            canonical = self._resolve_canonical_game_id(
                state,
                game_id,
                filename_hint=incoming.filename_hint,
                rom_sha1=incoming.rom_sha1,
            )
            incoming.game_id = canonical
            existing_raw = state.saves.get(canonical)
            existing = SaveMeta(game_id=canonical, **existing_raw) if existing_raw else None
            conflict = False

            if existing:
                if not force:
                    existing_time = parse_utc(existing.last_modified_utc)
                    incoming_time = parse_utc(incoming.last_modified_utc)

                    # equal timestamp but different payload should be preserved as conflict.
                    if incoming_time == existing_time and existing.sha256 != incoming.sha256:
                        conflict = True
                        incoming.conflict = True

                    # Client-claimed time older than index (common: handheld clock behind docked Switch).
                    # Same bytes: no-op. Different bytes: accept payload and re-stamp with server time so
                    # ordering matches reality (avoids silent 200 + no write + clients desyncing baselines).
                    if incoming_time < existing_time:
                        if existing.sha256 == incoming.sha256:
                            self._register_identity_maps(
                                state,
                                canonical=canonical,
                                upload_game_id=game_id,
                                filename_hint=incoming.filename_hint,
                                rom_sha1=incoming.rom_sha1,
                            )
                            self._write_index_state(state)
                            return existing, conflict, False, canonical
                        incoming.last_modified_utc = utc_now_iso()

                self._backup_existing(canonical, existing)

            target = self.save_path(canonical)
            tmp = target.with_suffix(".tmp")
            tmp.write_bytes(data)
            os.replace(tmp, target)

            incoming.server_updated_at = utc_now_iso()
            incoming.version = (existing.version + 1) if existing else 1
            state.saves[canonical] = incoming.model_dump(exclude={"game_id"})
            self._register_identity_maps(
                state,
                canonical=canonical,
                upload_game_id=game_id,
                filename_hint=incoming.filename_hint,
                rom_sha1=incoming.rom_sha1,
            )
            self._merge_duplicate_saves(state)
            self._write_index_state(state)
            return incoming, conflict, True, canonical

    def resolve_conflict(self, game_id: str) -> bool:
        with self._lock:
            state = self._read_index_state()
            canonical = self._resolve_canonical_game_id(state, game_id)
            raw = state.saves.get(canonical)
            if not raw:
                return False
            if not raw.get("conflict", False):
                return True
            raw["conflict"] = False
            state.saves[canonical] = raw
            self._write_index_state(state)
            return True

    def remove(self, game_id: str) -> bool:
        """Drop ``game_id`` from the index and delete ``{game_id}.sav`` if present."""
        with self._lock:
            state = self._read_index_state()
            canonical = self._resolve_canonical_game_id(state, game_id)
            if canonical not in state.saves:
                return False
            del state.saves[canonical]
            state.aliases = {k: v for k, v in state.aliases.items() if v != canonical}
            state.rom_sha1 = {k: v for k, v in state.rom_sha1.items() if v != canonical}
            state.tombstones = {k: v for k, v in state.tombstones.items() if v != canonical}
            self._write_index_state(state)
        path = self.save_path(canonical)
        if path.is_file():
            path.unlink()
        return True
