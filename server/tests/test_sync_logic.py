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
    effective, _, applied, _canonical = store.upsert(game_id, new_data, new_meta)
    assert applied is True
    assert effective.sha256 == new_meta.sha256
    assert store.get_bytes(game_id) == new_data


def test_older_client_timestamp_new_payload_still_replaces(tmp_path: Path) -> None:
    """Device clock behind index: different payload must still persist (not silent no-op)."""
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
    effective, _, applied, _canonical = store.upsert(game_id, older, older_meta)
    assert applied is True
    assert effective.sha256 == older_meta.sha256
    assert store.get_bytes(game_id) == older


def test_older_client_timestamp_same_payload_no_op(tmp_path: Path) -> None:
    store = SaveStore(
        save_root=tmp_path / "saves",
        history_root=tmp_path / "history",
        index_path=tmp_path / "index.json",
    )
    game_id = "same-ts-skip"
    data = b"bytes"
    meta_newer = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(500),
        sha256=hashlib.sha256(data).hexdigest(),
        size_bytes=len(data),
    )
    meta_older_claim = SaveMeta(
        game_id=game_id,
        last_modified_utc=_iso(400),
        sha256=hashlib.sha256(data).hexdigest(),
        size_bytes=len(data),
    )
    store.upsert(game_id, data, meta_newer)
    effective, _, applied, _canonical = store.upsert(game_id, data, meta_older_claim)
    assert applied is False
    assert effective.sha256 == meta_newer.sha256


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
    effective, conflict, applied, _canonical = store.upsert(game_id, b, meta_b)
    assert applied is True
    assert conflict is True
    assert effective.conflict is True


def test_decode_history_backup_stamp_from_stem() -> None:
    from app.storage import decode_history_backup_stamp_from_stem

    stem = "2026-03-17T21-00-00+00-00-" + "a" * 8
    assert decode_history_backup_stamp_from_stem(stem) == "2026-03-17T21:00:00+00:00"
    stem_z = "2026-03-17T21-00-00Z-" + "b" * 8
    assert decode_history_backup_stamp_from_stem(stem_z) == "2026-03-17T21:00:00Z"
    assert decode_history_backup_stamp_from_stem("not-a-valid-name") is None


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


def test_history_trims_to_max(tmp_path: Path) -> None:
    store = SaveStore(
        save_root=tmp_path / "saves",
        history_root=tmp_path / "history",
        index_path=tmp_path / "index.json",
        keep_history=True,
        history_max_per_game=2,
    )
    game_id = "trim-game"
    payloads = [b"a", b"b", b"c", b"d"]
    for i, payload in enumerate(payloads):
        meta = SaveMeta(
            game_id=game_id,
            last_modified_utc=f"2026-01-{10+i:02d}T00:00:00+00:00",
            sha256=hashlib.sha256(payload).hexdigest(),
            size_bytes=len(payload),
        )
        store.upsert(game_id, payload, meta)
    hist_files = list((tmp_path / "history" / game_id).glob("*.sav"))
    assert len(hist_files) <= 2


def test_history_pinned_survives_trim_over_unpinned_limit(tmp_path: Path) -> None:
    """Pinned backups are not deleted; only the newest N *unpinned* history files are kept."""
    store = SaveStore(
        save_root=tmp_path / "saves",
        history_root=tmp_path / "history",
        index_path=tmp_path / "index.json",
        keep_history=True,
        history_max_per_game=2,
    )
    game_id = "pin-survives"
    payloads = [b"a", b"b", b"c", b"d"]
    for i, payload in enumerate(payloads):
        meta = SaveMeta(
            game_id=game_id,
            last_modified_utc=f"2026-01-{10 + i:02d}T00:00:00+00:00",
            sha256=hashlib.sha256(payload).hexdigest(),
            size_bytes=len(payload),
        )
        store.upsert(game_id, payload, meta)
    hist = store.list_history(game_id)
    assert len(hist) == 2
    oldest_fn = hist[-1]["filename"]
    assert store.set_history_revision_keep(game_id, oldest_fn, True) is True
    meta5 = SaveMeta(
        game_id=game_id,
        last_modified_utc="2026-01-20T00:00:00+00:00",
        sha256=hashlib.sha256(b"e").hexdigest(),
        size_bytes=1,
    )
    store.upsert(game_id, b"e", meta5)
    hist2 = store.list_history(game_id)
    names = [e["filename"] for e in hist2]
    assert oldest_fn in names
    assert any(e["filename"] == oldest_fn and e.get("keep") for e in hist2)
    assert len(hist2) == 3
