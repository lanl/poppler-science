# Poppler-science: rich text extraction from (scientific) PDF files
================================

This is Poppler-science, an ongoing experiment to improve the exraction of rich text from PDF files. In this case, "rich text" refers to
Unicode text, superscripts, subscripts, and high-level document structure (i.e., headers, footers, left and right margin text, and text that
appears in tables and figures). Poppler-science is an experimental fork of the Poppler project (version 25.06.0), [README-Poppler](README-Poppler.md), which
in turn came from XPDF; see [README-XPDF](README-XPDF) for the original xpdf-3.03 README. Like Poppler, Poppler-science is [licensed under the GPL](LICENSE.txt).

The goal of Poppler-science is to accurately extract information from PDF files as quickly as possible. Benefits include improving the accuracy of retrieval augmented generation (RAG) applications and reducing false negatives when searching PDF files with text-based queries. To demonstrate proof-of-principle, a new version of pdftotext is provided by Poppler-science. Please note that the other Poppler utilities (i.e., pdftohtml, pdftoppm, etc.) have *not* been modified.

## Unicode text extraction
Doesn't Poppler (and every other PDF-to-text program) already extract Unicode characters from PDF files? 

The answer is, "sometimes".

For PDF files that contain pixel-based images, full-page optical character recognition (OCR) is needed to extract text. Neither Poppler-science nor Poppler performs full-page OCR and is *not* useful for extracting text from image-based PDFs (i.e., scanned documents). See tools like [Tesseract](https://github.com/tesseract-ocr) for extracting text from image-based PDF files. For PDF files that contain text information, most PDF-to-text tools only extract the text strings that are *reported* by the PDF file. These text strings can be (and often are) *different* than the strings *displayed* when the PDF file is graphically rendered. PDF creation software has the power to associate *any* Unicode value with *any* font glyph (and there are no checks to make sure that this mapping is correct).

Why would a software package generate a PDF that contains embedded text that does not match the displayed text? This is a great question for which I do not know the answer! There are, however, many examples of embeded text *not* matching displayed text. This is problem, since most (all?) of the commonly available PDF-to-text software tools (e.g., ) explicitly trust the source PDF file to contain embedded Unicode text strings that match the text that will be graphically displayed.

### Displayed != embedded text string example: Microsoft Word
Using a modern version of Microsoft Word for MacOS (Version 16.105):
- create a new document that contains a single word "difficult" in the "Aptos (Body)" font (which appears to be the default font circa early 2026)
- Save this document in as a PDF file. 
- Open this newly created PDF document in the MacOS "Preview" PDF viewer and copy the displayed word "difficult" to the clipboard.
- Paste the clipboard contents into a new Microsoft Word document.
- Instead of "difficult", you will see "di#icult".
  
What just happedened? When creating the PDF file, MS Word replaced the two adjacent "f" characters in "difficult" with a single Unicode character representing the "&#xFB00;" ligature (where a [ligature](https://en.wikipedia.org/wiki/Ligature_(writing)) contains multiple symbols/characters in a single font glyph). However, rather than embedding a valid Unicode code for "&#xFB00;" (= 0xFB00), MS Word embedded the Unicode symbol "#" (= 0x0023).

Note that the choice to replace two characters "ff" with a single ligature character "&#xFB00;" is font dependent. If the above example is repeated using the "Times New Roman" font in MS Word, the resulting PDF file does *not* contain a ligature and the embedded text matches the displayed text (as expected).

### Displayed != embedded text string example: Scientific literature
The final form for many (most?) scientific manuscripts is a PDF file. Scientific manuscripts often contain a mixture of many different Unicode symbol types (e.g., English, Greek, math symbols, etc.). When embedded characters don't match the displayed characters, the resulting extracted text can have dramatically different meaning. One example is when the displayed Greek symbol "&#x00B5;" (for micro) is embedded as "m". When this change happens in units of concentration (i.e., "3.3 &#x00B5;M"), the resulting translation error (i.e., "3.3 mM" instead of "3.3 &#x00B5;M") yields a *drastically* different concentration! Since both "&#x00B5;M" (micromolar) and "mM" (milimolar) are valid units of concentration, this error can be difficult to detect. There are recent research papers (Wei2025 and Moreira-Filho2025) that use OCR to correct these errors for symbols relevant to concentration units, but do not provide a more general solution for the diverse set of Unicode symbols that commonly appear in scientifc PDF files.

### Poppler-science strategy for accurate Unicode symbol extraction
As suggested in an internet post from 20XX, Poppler-science performs "per character" optical character recognition when extracting embedded text strings from PDF files. Unlike most existing pdf-to-text software tool, embedded Unicode values are not used. Instead each font glyph is internally rendered as a small bitmap image that is input to a multilayer perceptron algorithm to predict the corresponding Unicode value. Here are the details:
- The multilayer perceptron algorithm is only envoked when a new font glyph is encountered. Prediction results are stored in memory to allow fast lookup of Unicode values for font glyphs previously encountered in the current PDF file.
- The multilayer perceptron algorithm was trained by:
  - Extracting all font glyphs from XXX Open Access files downloaded from PubMed Central (PMC).
  - For each unique Unicode value, the set of PMC font glyphs were manually checked by visual inspection. Font glyphs that did not match Unicode value were excluded from the training set.
  - A two-layer perceptron, each layer with XXX and XXX nodes in each layer, was trained using Pytorch on an Apple M3 Mac Studio.
- The binary file of multilayer perceptron parameters (approximately 200 MB) are currently loaded from disk every time the Poppler-science pdftotext program is run.
- The inference of the Unicode value from an internal font glyph bitmap is implemented in C++ and performed using the CPU (using SIMD vector instructions). As a result, there is *no* dependancy on Pytorch software or GPU hardware.

## Superscript and subscript extraction

## Document structure extraction

# How to install Poppler-science

# How to run Poppler-science: pdftotext