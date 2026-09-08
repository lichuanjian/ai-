from pathlib import Path
import sys

from pdf2docx import Converter

repo = Path(__file__).parent / "caj2pdf"
sys.path.insert(0, str(repo))
from cajparser import CAJParser

src_dir = Path("E:/")
dst_dir = Path(r"D:\daimaxuexi")
work_dir = dst_dir / "_caj_work"
work_dir.mkdir(exist_ok=True)

files = sorted(src_dir.glob("*.caj"))
results = []
for src in files:
    pdf = work_dir / f"{src.stem}.pdf"
    docx = dst_dir / f"{src.stem}.docx"
    try:
        caj = CAJParser(str(src))
        caj.convert(str(pdf))
        cv = Converter(str(pdf))
        cv.convert(str(docx))
        cv.close()
        results.append((src.name, "ok", caj.page_num, docx.name, docx.stat().st_size))
    except Exception as exc:
        results.append((src.name, "error", type(exc).__name__, str(exc)))

for row in results:
    print("\t".join(map(str, row)))
