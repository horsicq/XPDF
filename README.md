# XPDF

## PDF JavaScript emulator (`xjs*`)

Self-contained JavaScript emulator for PDF/Acrobat scripts (malware triage / deobfuscation).
Built as part of XPDF; its use in `xpdf.cpp` is gated by `USE_PDFJSEMUL`
(CMake `WITH_PDFJSEMUL`, qmake `XCONFIG += use_pdfjsemul`).

Lexes, parses and **executes** embedded PDF JavaScript through a bounded tree-walking interpreter
with an Acrobat DOM (`app`, `util`, `this`/Doc, `Collab`, `spell`, `console`, `Math`, `String`, ...).
Multi-stage obfuscation (`eval` chains, `String.fromCharCode`, `unescape`, string concatenation,
`replace`) is unwound, and the dangerous API surface (`this.exportDataObject`, `Collab.getIcon`,
`util.printf`, `app.launchURL`, `this.submitForm`, ...) is captured into a structured report.

Qt-only (no XBinary dependency), so it can be embedded anywhere and unit-tested standalone.
Execution is bounded by step / recursion / loop / string caps, so hostile or non-terminating
scripts cannot hang or exhaust memory.

Entry point: `XJSEmul::analyze(source)` -> `XJS::XJSReport`, or `XJSEmul::analyzeToString(source)`.
Reached from `XPDF::getJavaScriptInfoString()`.
