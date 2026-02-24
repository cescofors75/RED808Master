from pathlib import Path
from shutil import copy2, copytree, rmtree
import gzip
from SCons.Script import COMMAND_LINE_TARGETS

Import("env")


def _is_uploadfs_target() -> bool:
    targets = {str(t).lower() for t in COMMAND_LINE_TARGETS}
    return "uploadfs" in targets or "buildfs" in targets


def _sync_data_to_data_gz(project_dir: Path) -> None:
    src = project_dir / "data"
    dst = project_dir / "data_gz"

    if not src.exists():
        print("[prepare_data_gz] data/ no existe, nada que preparar")
        return

    if dst.exists():
        rmtree(dst)

    copytree(src, dst)

    web_dir = dst / "web"
    if web_dir.exists():
        gz_created = 0
        for p in web_dir.rglob("*"):
            if p.is_file() and p.suffix in {".js", ".css", ".html"}:
                gz_path = Path(str(p) + ".gz")
                with p.open("rb") as fin:
                    raw = fin.read()
                with gzip.open(gz_path, "wb", compresslevel=9) as fout:
                    fout.write(raw)
                gz_created += 1

        removed = 0
        for p in web_dir.rglob("*"):
            if p.is_file() and p.suffix in {".js", ".css", ".html"} and not p.name.endswith(".gz"):
                p.unlink()
                removed += 1
        print(f"[prepare_data_gz] data_gz listo. Generados {gz_created} .gz, eliminados {removed} assets web sin comprimir")
    else:
        print("[prepare_data_gz] warning: no se encontró data/web en staging")


if _is_uploadfs_target():
    project = Path(env.subst("$PROJECT_DIR"))
    _sync_data_to_data_gz(project)
else:
    print("[prepare_data_gz] omitido (solo actúa en uploadfs/buildfs)")
