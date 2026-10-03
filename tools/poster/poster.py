#!/usr/bin/env python3
"""
poster.py: a reference sheet for Machdyne BASIC and Sechs, generated from
their documentation, so that the two never disagree: one A4 sheet, printed
on both sides (the language on the front, Sechs on the back).

    python3 tools/poster/poster.py                         # poster.html
    python3 tools/poster/poster.py --pdf poster.pdf        # and a PDF

The content comes from docs/basic1.md (Machdyne BASIC 1), docs/targets.md
and docs/sechs.md (the Sechs specification): their tables, lists and headings. If a section the
poster needs is missing or renamed, the script stops and says which, so
that the poster is never silently out of date. Only layout lives here.

The HTML needs nothing but this script. The PDF needs WeasyPrint
(pip install weasyprint).
"""

import argparse
import datetime
import html
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))


# ---- reading the documents -------------------------------------------------

class Doc:
    """A Markdown document, split into sections by its headings."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        with open(path, encoding="utf-8") as f:
            self.text = f.read()
        self.sections = []          # (level, title, body)
        title, level, body = "", 0, []
        for line in self.text.split("\n"):
            m = re.match(r"^(#{1,4})\s+(.*)$", line)
            if m:
                self.sections.append((level, title, "\n".join(body)))
                level, title, body = len(m.group(1)), m.group(2).strip(), []
            else:
                body.append(line)
        self.sections.append((level, title, "\n".join(body)))

    def section(self, title):
        """The body of the section whose title (without its number) starts
        with `title`, including its subsections."""
        for i, (level, t, body) in enumerate(self.sections):
            plain = re.sub(r"^[\d.]+\s+", "", t)
            if plain.lower().startswith(title.lower()):
                parts = [body]
                for lv, t2, b2 in self.sections[i + 1:]:
                    if lv <= level:
                        break
                    parts.append(b2)
                return "\n".join(parts)
        sys.exit("%s: no section '%s' (the poster needs it)" % (self.name, title))

    def match(self, pattern, what):
        m = re.search(pattern, self.text, re.M)
        if not m:
            sys.exit("%s: cannot find %s" % (self.name, what))
        return m.group(1)


def tables(body):
    """The tables in a section: lists of rows, header first."""
    out, cur = [], []
    for line in body.split("\n"):
        if line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if not all(re.match(r"^:?-+:?$", c) for c in cells):
                cur.append(cells)
        elif cur:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return out


def table(body, first_header, where):
    for t in tables(body):
        if t[0][0].lower() == first_header.lower():
            return t
    sys.exit("%s: no table starting with '%s'" % (where, first_header))


def items(body, numbered=False, nested=None):
    """Top-level list items, continuation lines joined. If `nested` is a
    list, each item's indented sub-items are appended to it (as lists)."""
    out, sub = [], None
    pat = r"^\d+\.\s+(.*)" if numbered else r"^[-*]\s+(.*)"
    for line in body.split("\n"):
        m = re.match(pat, line)
        n = re.match(r"^\s+[-*]\s+(.*)", line)
        if m:
            out.append(m.group(1))
            sub = None
            if nested is not None:
                nested.append([])
        elif n and out:
            sub = n.group(1)
            if nested is not None:
                nested[-1].append(sub)
        elif out and line.startswith("  ") and line.strip():
            if sub is not None:
                if nested is not None:
                    nested[-1][-1] += " " + line.strip()
            else:
                out[-1] += " " + line.strip()
        elif not line.strip():
            sub = None if not out else sub
    return out


def lead(item):
    """'**Lead** rest' -> (lead, rest). Punctuation right after the bold
    part stays with the lead ('Boot window,')."""
    m = re.match(r"^\*\*(.+?)\*\*([,;]?)[:.]?\s*(.*)$", item)
    if not m:
        return ("", item)
    return (m.group(1).rstrip(":.") + m.group(2), m.group(3))


def first_sentence(s):
    m = re.match(r"^(.+?[.!?])(\s|$)", s)
    return m.group(1) if m else s


def inline(md):
    """Markdown inline -> HTML: code, bold, links (as text)."""
    out, pos = [], 0
    for m in re.finditer(r"`([^`]+)`", md):
        out.append(_text(md[pos:m.start()]))
        out.append("<code>%s</code>" % html.escape(m.group(1)))
        pos = m.end()
    out.append(_text(md[pos:]))
    return "".join(out)


def _text(s):
    s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
    s = html.escape(s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    return s


def bits(text):
    """'bit 0 alive, 1 no fault, ...' -> {0: 'alive', 1: 'no fault'}."""
    m = re.search(r"bit\s+(\d.*)", text)
    if not m:
        return {}
    out = {}
    for part in m.group(1).split(","):
        p = re.match(r"\s*(\d+)\s+(.*)", part)
        if p:
            out[int(p.group(1))] = p.group(2).strip()
    return out


# ---- building blocks of the page --------------------------------------------

def h_table(rows, cols=None, classes=""):
    """An HTML table from parsed rows (header first)."""
    head, body = rows[0], rows[1:]
    idx = cols if cols is not None else range(len(head))
    s = ['<table class="%s"><tr>' % classes]
    s += ["<th>%s</th>" % inline(head[i]) for i in idx]
    s.append("</tr>")
    for r in body:
        s.append("<tr>" + "".join("<td>%s</td>" % inline(r[i]) for i in idx) + "</tr>")
    s.append("</table>")
    return "".join(s)


def block(title, content, kind=""):
    return '<section class="block %s"><h3>%s</h3>%s</section>' % (
        kind, html.escape(title), content)


def bitstrip(name, reg, text, kind):
    b = bits(text)
    cells = []
    for i in range(7, -1, -1):
        label = b.get(i, "")
        cells.append('<div class="bit%s"><span class="n">%d</span>%s</div>' % (
            "" if label else " unused", i, inline(label)))
    return ('<div class="bitreg %s"><div class="bitname"><code>%s</code> %s</div>'
            '<div class="bits">%s</div></div>') % (kind, reg, html.escape(name), "".join(cells))


def connector(pinout):
    """The six-pin connector, drawn from the pinout table."""
    groups = {"A": "bus", "B": "bus", "C": "local", "D": "local",
              "GND": "gnd", "3V3": "pwr"}
    pads = []
    for row in pinout[1:]:
        pin, name, func = row[0], row[1], row[2]
        g = groups.get(name, "gnd")
        pads.append(
            '<div class="pad %s"><div class="pnum">%s</div><div class="pname">%s</div>'
            '<div class="pfunc">%s</div></div>' % (g, html.escape(pin), html.escape(name), inline(func)))
    return '<div class="connector">%s</div>' % "".join(pads)


# ---- the page --------------------------------------------------------------------

CSS = """
@page { size: A4 portrait; margin: 8mm 8mm 7mm 8mm; }
.page { break-after: page; }
.page:last-child { break-after: auto; }
* { box-sizing: border-box; }
html { font-family: "DejaVu Sans Condensed", "DejaVu Sans", "Liberation Sans", Arial, sans-serif;
       font-size: 7pt; line-height: 1.3; color: #1b2433; background: #fff; }
body { margin: 0; }
code, .mono { font-family: "DejaVu Sans Mono", "Liberation Mono", monospace; font-size: 0.94em; }
:root { --ink: #1b2433; --muted: #5b6577; --rule: #d5dae2;
        --bus: #0f766e; --bus-t: #e3f1ef; --local: #b45309; --local-t: #fbefe0;
        --pwr: #9f1239; --basic: #3b3f99; --basic-t: #eceefa; }

header { display: flex; align-items: flex-end; justify-content: space-between;
         border-bottom: 1.2pt solid var(--ink); padding-bottom: 1.8mm; margin-bottom: 2.5mm; }
header h1 { font-size: 22pt; line-height: 1; margin: 0; letter-spacing: -0.4pt; font-weight: bold;
            white-space: nowrap; }
header .sub { font-size: 8.5pt; color: var(--muted); margin-top: 0.8mm; }
header .legend span { white-space: nowrap; }
header .legend { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 1mm 3mm;
                 font-size: 7pt; max-width: 75mm; }
header h1 { font-size: 20pt; }
header .legend span::before { content: ""; display: inline-block; width: 3mm; height: 3mm;
         border-radius: 0.6mm; margin-right: 1.2mm; vertical-align: -0.5mm; }
.lg-bus::before { background: var(--bus); } .lg-local::before { background: var(--local); }
.lg-pwr::before { background: var(--pwr); } .lg-basic::before { background: var(--basic); }

.half > h2 { display: none; }
.half > h2 { font-size: 13pt; margin: 0 0 1.5mm 0; }
.basic > h2 { color: var(--basic); }
.sechs > h2 { color: var(--bus); }
.cols { column-count: 2; column-gap: 4mm; }

.block { break-inside: avoid; margin: 0 0 2.6mm 0; }
.block h3 { font-size: 8.4pt; margin: 0 0 0.8mm 0; padding-bottom: 0.4mm;
            border-bottom: 0.6pt solid var(--rule); }
.basic .block h3 { color: var(--basic); }
.block.bus h3 { color: var(--bus); } .block.local h3 { color: var(--local); }

table { width: 100%; border-collapse: collapse; }
th { text-align: left; font-weight: bold; color: var(--muted); font-size: 0.92em;
     padding: 0.3mm 1mm 0.3mm 0; border-bottom: 0.5pt solid var(--rule); }
td { padding: 0.35mm 1mm 0.35mm 0; vertical-align: top; border-bottom: 0.3pt solid #eef0f4; }
td:first-child { white-space: nowrap; }
table.wrap td:first-child { white-space: normal; }
.basic td code { color: var(--basic); }
.facts { margin: 0; padding: 0; list-style: none; }
.facts li { margin: 0 0 0.45mm 0; }
.prec { display: flex; flex-wrap: wrap; gap: 0.8mm; align-items: center; }
.prec span { background: var(--basic-t); border-radius: 0.8mm; padding: 0.3mm 1.2mm; }
.prec span code { color: var(--basic); font-weight: bold; }
.prec i { color: var(--muted); font-style: normal; }
.facts.sub { margin-top: 0.4mm; padding-left: 2.5mm; }
.facts.sub li { margin: 0; list-style: disc; }
.facts b { color: var(--ink); }
ol.steps { margin: 0; padding-left: 4mm; }
ol.steps li { margin-bottom: 0.6mm; }
p { margin: 0 0 1mm 0; }
.note { color: var(--muted); }

.connector { display: flex; gap: 1.2mm; margin: 0 0 3mm 0; }
.pad { flex: 1; border-radius: 1.4mm; padding: 1.4mm 1.6mm 1.6mm; color: #fff; min-height: 21mm; }
.pad.bus { background: var(--bus); } .pad.local { background: var(--local); }
.pad.pwr { background: var(--pwr); } .pad.gnd { background: var(--ink); }
.pnum { font-size: 7pt; opacity: 0.8; }
.pname { font-size: 19pt; font-weight: bold; line-height: 1.05; margin-bottom: 0.6mm; }
.pfunc { font-size: 6.6pt; line-height: 1.25; }
.pfunc code { color: #fff; }

.bitreg { margin: 0 0 1.4mm 0; }
.bitname { margin-bottom: 0.4mm; }
.bitreg.bus .bitname code { color: var(--bus); font-weight: bold; }
.bits { display: flex; gap: 0.5mm; }
.bit { flex: 1; background: var(--bus-t); border-radius: 0.6mm; padding: 0.4mm 0.6mm 0.6mm;
       font-size: 5.9pt; line-height: 1.15; min-height: 8.2mm; }
.bit.unused { background: #f3f4f7; color: #a0a7b4; }
.bit .n { display: block; font-size: 5.6pt; color: var(--bus); font-weight: bold; }
.bit.unused .n { color: #a0a7b4; }

.modes td:first-child code { font-weight: bold; color: var(--local); }
.regs td:first-child code, .regs td:nth-child(2) { color: var(--bus); font-weight: bold; }
.regs td:nth-child(2) { white-space: nowrap; }

footer { margin-top: 1mm; padding-top: 1.2mm; border-top: 0.6pt solid var(--rule);
         color: var(--muted); font-size: 6.2pt; display: flex; justify-content: space-between; }
"""


def build(basic, sechs, targets, stamp):
    b_title = basic.match(r"^# (.+)$", "the title")
    s_ver = sechs.match(r"Status: (Draft [\d.]+)", "the status line")

    # ---- Machdyne BASIC ----
    lines = basic.section("Programs and lines")
    values = basic.section("Values")
    facts = []
    for it in items(lines) + items(values):
        l, rest = lead(it)
        if l:
            facts.append("<li><b>%s</b> %s</li>" % (inline(l), inline(first_sentence(rest))))
    expr = basic.section("Expressions")
    stm = basic.section("Statements")
    left = [
        block("Programs and values", '<ul class="facts">%s</ul>' % "".join(facts)),
        block("Statements", h_table(table(stm, "Statement", "basic1.md: Statements"))),
        block("Functions", h_table(table(expr, "Function", "basic1.md: Expressions"), classes="wrap")),
        block("Operators, highest precedence first", '<div class="prec">%s</div>' % '<i>&gt;</i>'.join(
            "<span>%s</span>" % inline(r[1]) for r in
            table(expr, "Precedence (highest first)", "basic1.md: Expressions")[1:])),
        block("Commands", h_table(table(basic.section("Commands"), "Command", "basic1.md: Commands"))),
        block("Pins", h_table(table(basic.section("Pins"), "Mode", "basic1.md: Pins"),
                              classes="modes"), "local"),
        block("Data files", '<ul class="facts">%s</ul>' % "".join(
            "<li>%s</li>" % inline(first_sentence(i)) for i in items(basic.section("Data files")))),
        block("Limits", h_table(table(basic.section("Limits"), "Limit", "basic1.md: Limits"))),
        block("Errors", h_table(table(basic.section("Errors"), "Message", "basic1.md: Errors"),
                                classes="wrap")),
    ]
    if targets:
        left.append(block("Machines", h_table(table(targets.text, "Target", "targets.md"),
                                              cols=[0, 1, 2, 3], classes="wrap")))
        left.append(block("Werkzeug pins (PIN n, mode for 5-24)", h_table(
            table(targets.section("Werkzeug"), "Pins", "targets.md: Werkzeug")), "local"))

    # ---- Sechs ----
    pinout = table(sechs.section("Pinout"), "Pin", "sechs.md: Pinout")
    regs = table(sechs.section("Core registers"), "Reg", "sechs.md: Core registers")
    reg = {r[1]: r for r in regs[1:]}
    for need in ("CAPS", "STATUS", "OK"):
        if need not in reg:
            sys.exit("sechs.md: core register %s missing" % need)
    plain_regs = [regs[0]] + [r for r in regs[1:] if r[1] not in ("CAPS", "STATUS", "OK")]
    # "Invariant N" in the text -> that invariant's title
    inv_titles = [lead(it)[0].rstrip(",;") for it in
                  items(sechs.section("Safety invariants"), numbered=True)]

    def resolve(t):
        return re.sub(r"[Ii]nvariant (\d+)", lambda m: (
            inv_titles[int(m.group(1)) - 1].lower() + " (invariant %s)" % m.group(1))
            if 0 < int(m.group(1)) <= len(inv_titles) else m.group(0), t)

    steps, subs = [], []
    for i, it in enumerate(items(sechs.section("Power-on sequence"), True, subs)):
        l, rest = lead(resolve(it))
        sub = "".join("<li>%s</li>" % inline(t.rstrip(",.")) for t in subs[i])
        steps.append("<li><b>%s</b> %s%s</li>" % (
            inline(l), inline(first_sentence(rest)),
            '<ul class="facts sub">%s</ul>' % sub if sub else ""))
    inv = []
    for it in items(sechs.section("Safety invariants"), numbered=True):
        l, rest = lead(it)
        inv.append("<li><b>%s</b> %s</li>" % (inline(l), inline(first_sentence(rest))))
    consoles = sechs.section("Consoles")
    uart = []
    for it in items(consoles):
        l, rest = lead(it)
        uart.append("<li>%s</li>" % (("<b>%s</b> %s" % (inline(l), inline(first_sentence(rest))))
                                      if l else inline(first_sentence(it))))
    files = sechs.section("Files through the consoles")
    progregs = sechs.section("Program registers").strip().split("\n\n")[0]
    right = [
        block("Power-on", '<ol class="steps">%s</ol>' % "".join(steps), "bus"),
        block("Core registers", h_table(plain_regs, cols=[0, 1, 3], classes="regs wrap")
              + bitstrip("capabilities", "CAPS", reg["CAPS"][3], "bus")
              + bitstrip("status", "STATUS", reg["STATUS"][3], "bus")
              + bitstrip("all good if every bit is 1", "OK", reg["OK"][3], "bus"), "bus"),
        block("Identity (INFO)", h_table(table(sechs.section("INFO"), "Key", "sechs.md: INFO")), "bus"),
        block("Program registers", "<p>%s</p>" % inline(" ".join(progregs.split())), "bus"),
        block("I2C console", h_table(table(consoles, "Reg", "sechs.md: Consoles"),
                                     cols=[0, 1, 3], classes="regs"), "bus"),
        block("UART console", '<ul class="facts">%s</ul>' % "".join(uart), "local"),
        block("Files through the consoles", h_table(table(files, "Command", "sechs.md: Files")), "bus"),
        block("Pin declaration", h_table(table(sechs.section("Pin declaration"), "Mode",
                                               "sechs.md: Pin declaration"), classes="modes wrap"), "local"),
        block("Safety invariants", '<ol class="steps">%s</ol>' % "".join(inv)),
    ]

    def header(title, sub, legend):
        return ('<header><div><h1>%s</h1><div class="sub">%s</div></div>'
                '<div class="legend">%s</div></header>' % (title, sub, legend))

    legend_basic = ('<span class="lg-basic">language</span>'
                    '<span class="lg-local">pins</span>')
    legend_sechs = ('<span class="lg-bus">global bus (pins A, B)</span>'
                    '<span class="lg-local">local I/O (pins C, D)</span>'
                    '<span class="lg-pwr">power</span>')
    foot = ('<footer><span>Generated from %s, %s and %s by tools/poster/poster.py. '
            'The documents are the reference.</span><span>%s</span></footer>') % (
        html.escape(basic.name), "targets.md" if targets else "-",
        html.escape(sechs.name), html.escape(stamp))

    return """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<title>Machdyne BASIC and Sechs</title>
<style>%s</style></head><body>
<div class="page">%s<div class="half basic"><div class="cols">%s</div></div>%s</div>
<div class="page">%s<div class="half sechs">%s<div class="cols">%s</div></div>%s</div>
</body></html>
""" % (CSS,
       header(html.escape(b_title), "The language, on one page. Sechs is on the back.",
              legend_basic),
       "".join(left), foot,
       header("Sechs " + html.escape(s_ver.replace("Draft ", "")),
              "The six-pin interface: pins, power-on, registers, consoles.", legend_sechs),
       connector(pinout), "".join(right), foot)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--basic", default=os.path.join(REPO, "docs", "basic1.md"))
    ap.add_argument("--targets", default=os.path.join(REPO, "docs", "targets.md"))
    ap.add_argument("--sechs", default=os.path.join(REPO, "docs", "sechs.md"))
    ap.add_argument("--html", default="poster.html")
    ap.add_argument("--pdf", help="also write a PDF (needs WeasyPrint)")
    a = ap.parse_args()

    sechs_path = a.sechs
    for p in (a.basic, sechs_path):
        if not os.path.exists(p):
            sys.exit("not found: %s (see --help)" % p)

    try:
        rev = subprocess.run(["git", "-C", REPO, "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True).stdout.strip()
    except OSError:
        rev = ""
    stamp = datetime.date.today().isoformat() + (" (basic %s)" % rev if rev else "")

    targets = Doc(a.targets) if os.path.exists(a.targets) else None
    page = build(Doc(a.basic), Doc(sechs_path), targets, stamp)
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
        print("wrote %s (%d pages: front and back)" % (a.pdf, len(doc.pages)))
        if len(doc.pages) > 2:
            print("warning: the content no longer fits one sheet (two pages)",
                  file=sys.stderr)


if __name__ == "__main__":
    main()
