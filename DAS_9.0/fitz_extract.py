try:
 import fitz
 print('fitz ok',fitz.__doc__[:30])
 d=fitz.open('peng_source.pdf'); print('pages',d.page_count)
 with open('peng_pdf_text.txt','w',encoding='utf-8') as f:
  for i in range(d.page_count):
   f.write(f'\n===== PDF PAGE {i+1} =====\n'); f.write(d.load_page(i).get_text())
except Exception as e: print(type(e).__name__,e)
