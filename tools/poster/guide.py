#!/usr/bin/env python3
"""
guide.py: the getting-started guide (docs/guide.md) as a printed A4 page,
in the style of the reference sheet (poster.py).

    python3 tools/poster/guide.py                      # guide.html
    python3 tools/poster/guide.py --pdf guide.pdf      # and a PDF

The guide is the document; this script only lays it out. The programs in
it are checked by the test suite (testsuite.sh). The PDF needs WeasyPrint.
"""

import argparse
import html
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from poster import REPO, inline, tables  # noqa: E402

CSS = """
@page { size: A4 portrait; margin: 10mm 11mm 9mm 11mm; }
* { box-sizing: border-box; }
html { font-family: "DejaVu Sans Condensed", "DejaVu Sans", "Liberation Sans", Arial, sans-serif;
       font-size: 8.6pt; line-height: 1.38; color: #1b2433; background: #fff; }
body { margin: 0; }
code { font-family: "DejaVu Sans Mono", "Liberation Mono", monospace; font-size: 0.92em; }
:root { --ink: #1b2433; --muted: #5b6577; --rule: #d5dae2;
        --bus: #0f766e; --bus-t: #e3f1ef; --local: #b45309;
        --basic: #3b3f99; --basic-t: #eceefa; }

header { border-bottom: 1.2pt solid var(--ink); padding-bottom: 2mm; margin-bottom: 4mm;
         display: flex; justify-content: space-between; align-items: flex-end; }
header h1 { font-size: 24pt; line-height: 1; margin: 0; letter-spacing: -0.4pt; }
header .sub { color: var(--muted); font-size: 9pt; margin-top: 1mm; }
.intro { font-size: 9.4pt; margin: 0 0 4mm 0; }

.cols { column-count: 2; column-gap: 7mm; }
section.step { break-inside: avoid; margin: 0 0 4.2mm 0; }
section.step h2 { font-size: 11pt; margin: 0 0 1.4mm 0; display: flex; align-items: center; gap: 2.2mm; }
section.step h2 .n { display: inline-block; width: 6.2mm; height: 6.2mm; border-radius: 50%;
                     background: var(--basic); color: #fff; font-size: 9pt; text-align: center;
                     line-height: 6.2mm; flex: none; }
p { margin: 0 0 1.6mm 0; }
ul { margin: 0 0 1.6mm 0; padding-left: 4mm; }
li { margin-bottom: 0.8mm; }
pre { margin: 0 0 1.8mm 0; padding: 1.6mm 2.4mm; border-radius: 1.2mm; white-space: pre-wrap;
      font-family: "DejaVu Sans Mono", "Liberation Mono", monospace; font-size: 8.2pt; line-height: 1.35; }
pre.basic { background: var(--basic-t); color: var(--basic); border-left: 1mm solid var(--basic); }
pre.shell { background: #f2f4f7; color: var(--ink); border-left: 1mm solid var(--bus); }
p code, li code, td code { background: #f2f4f7; padding: 0 0.3mm; border-radius: 0.5mm; }
table { width: 100%; border-collapse: collapse; margin: 0 0 1.8mm 0; font-size: 8.2pt; }
th { text-align: left; color: var(--muted); border-bottom: 0.5pt solid var(--rule); padding: 0.4mm 1mm 0.4mm 0; }
td { border-bottom: 0.3pt solid #eef0f4; padding: 0.5mm 1mm 0.5mm 0; vertical-align: top; }
footer { margin-top: 2mm; padding-top: 1.5mm; border-top: 0.6pt solid var(--rule);
         color: var(--muted); font-size: 7pt; }
"""


def render(md):
    """The guide's Markdown (headings, paragraphs, lists, tables, code) as
    HTML: an introduction, then one block per numbered section."""
    title, intro, steps = "", [], []
    cur = intro
    lines = md.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("# "):
            title = line[2:].strip()
        elif line.startswith("## "):
            m = re.match(r"(\d+)\.\s+(.*)", line[3:].strip())
            n, t = (m.group(1), m.group(2)) if m else ("", line[3:].strip())
            steps.append([n, t, []])
            cur = steps[-1][2]
        elif line.startswith("```"):
            kind = "basic" if line[3:].strip() == "basic" else "shell"
            body = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                body.append(lines[i])
                i += 1
            cur.append('<pre class="%s">%s</pre>' % (kind, html.escape("\n".join(body))))
        elif line.startswith("|"):
            block = []
            while i < len(lines) and lines[i].startswith("|"):
                block.append(lines[i])
                i += 1
            rows = tables("\n".join(block))[0]
            s = "<table><tr>%s</tr>" % "".join("<th>%s</th>" % inline(c) for c in rows[0])
            for r in rows[1:]:
                s += "<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in r)
            cur.append(s + "</table>")
            continue
        elif line.startswith("- "):
            items = []
            while i < len(lines) and (lines[i].startswith("- ") or lines[i].startswith("  ")):
                if lines[i].startswith("- "):
                    items.append(lines[i][2:])
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            cur.append("<ul>%s</ul>" % "".join("<li>%s</li>" % inline(t) for t in items))
            continue
        elif line.strip():
            para = [line.strip()]
            while i + 1 < len(lines) and lines[i + 1].strip() and \
                    not re.match(r"^(#|```|\||- )", lines[i + 1]):
                i += 1
                para.append(lines[i].strip())
            cur.append("<p>%s</p>" % inline(" ".join(para)))
        i += 1
    if not title or not steps:
        sys.exit("guide.md: needs a # title and ## numbered sections")
    body = "".join(
        '<section class="step"><h2><span class="n">%s</span>%s</h2>%s</section>' % (
            html.escape(n), inline(t), "".join(content)) for n, t, content in steps)
    return title, "".join(intro), body


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--guide", default=os.path.join(REPO, "docs", "guide.md"))
    ap.add_argument("--html", default="guide.html")
    ap.add_argument("--pdf", help="also write a PDF (needs WeasyPrint)")
    a = ap.parse_args()
    with open(a.guide, encoding="utf-8") as f:
        title, intro, body = render(f.read())
    page = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>%s</title><style>%s</style></head><body>
<header><div><h1>%s</h1><div class="sub">Machdyne BASIC with Werkzeug and an LS10A module</div></div></header>
<div class="intro">%s</div>
<div class="cols">%s</div>
<footer>From docs/guide.md, laid out by tools/poster/guide.py. Every program here is checked by the test suite.</footer>
</body></html>
""" % (html.escape(title), CSS, html.escape(title), intro, body)
    with open(a.html, "w", encoding="utf-8") as f:
        f.write(page)
    print("wrote", a.html)
    if a.pdf:
        try:
            import weasyprint
        except ImportError:
            sys.exit("--pdf needs WeasyPrint: pip install weasyprint")
        doc = weasyprint.HTML(string=page).render()
        doc.write_pdf(a.pdf)
        print("wrote %s (%d page%s)" % (a.pdf, len(doc.pages), "" if len(doc.pages) == 1 else "s"))


if __name__ == "__main__":
    main()
