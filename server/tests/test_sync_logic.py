import hashlib
from datetime import datetime, timezone
from pathlib import Path

from app.models import SaveMeta
from app.storage import SaveStore


def _iso(seconds: int) -> str:
    return datetime.fromtimestamp(seconds, timezone.utc).replace(microsecond=0).isoformat()


def test_newer_upload_replaces_older(tmp_path: Path) -> None:
    store = SaveStore(
        save_root=tmp_path / "saves",
        history_root=tmp_path / "history",
        index_path=tmp_path / "index.json",
    )
    game_id = "pokemon-emerald"
    old_data = b"old"
    new_data = b"new"

    old_meta = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(100),
        sha256=hashlib.sha256(old_data).hexdigest(),
        size_bytes=len(old_data),
    )
    new_meta = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(200),
        sha256=hashlib.sha256(new_data).hexdigest(),
        size_bytes=len(new_data),
    )

    store.upsert(game_id, old_data, old_meta)
    effective, _ = store.upsert(game_id, new_data, new_meta)
    assert effective.sha256 == new_meta.sha256
    assert store.get_bytes(game_id) == new_data


def test_older_upload_is_ignored(tmp_path: Path) -> None:
    store = SaveStore(
        save_root=tmp_path / "saves",
        history_root=tmp_path / "history",
        index_path=tmp_path / "index.json",
    )
    game_id = "metroid-zero"
    newer = b"newer"
    older = b"older"
    newer_meta = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(400),
        sha256=hashlib.sha256(newer).hexdigest(),
        size_bytes=len(newer),
    )
    older_meta = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(300),
        sha256=hashlib.sha256(older).hexdigest(),
        size_bytes=len(older),
    )
    store.upsert(game_id, newer, newer_meta)
    effective, _ = store.upsert(game_id, older, older_meta)
    assert effective.sha256 == newer_meta.sha256
    assert store.get_bytes(game_id) == newer


def test_equal_timestamp_different_hash_marks_conflict(tmp_path: Path) -> None:
    store = SaveStore(
        save_root=tmp_path / "saves",
        history_root=tmp_path / "history",
        index_path=tmp_path / "index.json",
    )
    game_id = "zelda-minish"
    t = _iso(500)
    a = b"a"
    b = b"b"
    meta_a = SaveMeta(
        game_id=game_id,
        last_modified_utc=t,
        sha256=hashlib.sha256(a).hexdigest(),
        size_bytes=len(a),
    )
    meta_b = SaveMeta(
        game_id=game_id,
        last_modified_utc=t,
        sha256=hashlib.sha256(b).hexdigest(),
        size_bytes=len(b),
    )
    store.upsert(game_id, a, meta_a)
    effective, conflict = store.upsert(game_id, b, meta_b)
    assert conflict is True
    assert effective.conflict is True


def test_history_backup_is_created_on_replace(tmp_path: Path) -> None:
    store = SaveStore(
        save_root=tmp_path / "saves",
        history_root=tmp_path / "history",
        index_path=tmp_path / "index.json",
        keep_history=True,
    )
    game_id = "fzero"
    old_data = b"old-save"
    new_data = b"new-save"
    old_meta = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(100),
        sha256=hashlib.sha256(old_data).hexdigest(),
        size_bytes=len(old_data),
    )
    new_meta = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(200),
        sha256=hashlib.sha256(new_data).hexdigest(),
        size_bytes=len(new_data),
    )

    store.upsert(game_id, old_data, old_meta)
    store.upsert(game_id, new_data, new_meta)
    backups = list((tmp_path / "history" / game_id).glob("*.sav"))
    assert backups
