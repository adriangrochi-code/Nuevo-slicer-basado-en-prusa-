# PyInstaller spec of the tisma-cloud executable: master server, computing node and administration in one program.
# Built by .github/workflows/server.yml for Windows and Linux: pip install ".[master,exe]" && pyinstaller tisma-cloud.spec
from PyInstaller.utils.hooks import collect_all, collect_submodules

datas = [("tisma_cloud/static/*.html", "tisma_cloud/static")]
binaries = []
# Modules imported by name at run time: the protocols and loops of uvicorn, the binary driver of PostgreSQL.
hiddenimports = collect_submodules("uvicorn") + collect_submodules("tisma_cloud")
for package in ("psycopg", "psycopg_binary"):
    d, b, h = collect_all(package)
    datas, binaries, hiddenimports = datas + d, binaries + b, hiddenimports + h

a = Analysis(["tisma_cloud_exe.py"], pathex=["."], binaries=binaries, datas=datas, hiddenimports=hiddenimports,
             excludes=["pytest", "tkinter"])
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name="tisma-cloud", console=True, upx=False)
