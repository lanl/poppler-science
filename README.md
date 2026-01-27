# Poppler-science: rich text extraction from (scientific) PDF files

This is Poppler-science, an ongoing experiment to improve the extraction of rich text from PDF files. In this case, "rich text" refers to
[Unicode](https://en.wikipedia.org/wiki/List_of_Unicode_characters) text, superscripts, subscripts, and high-level document structure 
(i.e., headers, footers, left and right margin text, and text that
appears in tables and figures). Poppler-science is an experimental fork of the Poppler project (version 25.06.0), [README-Poppler](README-Poppler.md), which
in turn came from XPDF; see [README-XPDF](README-XPDF) for the original xpdf-3.03 README. Like Poppler, Poppler-science is [licensed under the GPL](LICENSE.txt).

The goal of Poppler-science is to accurately extract text-based information from PDF files as quickly as possible. Benefits include improving the accuracy of retrieval augmented generation (RAG) applications and reducing false negatives when searching PDF files with text-based queries. To demonstrate proof-of-principle, a new version of `pdftotext` is provided by Poppler-science. Please note that the other Poppler utilities (i.e., `pdftohtml`, `pdftoppm`, etc.) have *not* been modified.

## Key features of Poppler-science include:
- An integrated multilayer perceptron to predict Unicode values from *individual* font glyph bitmaps -- this is "per character" optical character recognition (OCR).
- Superscript and subscript text identification based on text position and size using simple coding heuristics. 
- Per-page text string ordering inference using single linkage clustering. 
- High-level document structure inference (using location and word density-based heuristics) at the level of:
  - Header
  - Footer
  - Left margin
  - Right margin
  - "Data" -- which can be either a table or figure. While many scientific PDF files use bitmap-based figures, it is not uncommon for a bitmap figure to also have a text overlay, which will be extracted.

### Please note that for PDF files that contain pixel-based images, full-page optical character recognition (OCR) is needed to extract text from these images. Neither Poppler-science nor Poppler performs full-page OCR and are *not* useful for extracting text from purely image-based PDFs (i.e., scanned documents). Checkout tools like [Tesseract](https://github.com/tesseract-ocr) for extracting text from bitmapped images.

## Accurate Unicode text extraction
Doesn't Poppler (and [every other PDF-to-text program](https://en.wikipedia.org/wiki/List_of_PDF_software)) already extract Unicode characters from PDF files? 

The answer is, "most of the time, but not always".

 For PDF files that contain text information, most PDF-to-text tools only extract the text strings that are *reported* by the PDF file. These text strings can be (and often are) *different* than the strings *displayed* when the PDF file is graphically rendered. PDF creation software has the power to associate *any* Unicode value with *any* font glyph (and there are no checks to make sure that this mapping is correct).

Why would a software package generate a PDF that contains embedded text that does not match the displayed text? This is a great question for which I do not know the answer! [There](https://stackoverflow.com/questions/28678841/pdf-text-extraction-returns-wrong-characters-due-to-tounicode-map) [are](https://lists.freedesktop.org/archives/poppler/2012-April/009035.html), [however](https://lists.apache.org/thread/gzxw7lv9yqjox4q59dojwr6533lbnbnc), [many](https://stackoverflow.com/questions/9224211/extract-text-from-corrupt-pdf-document) [examples](https://stackoverflow.com/questions/12184304/extracting-text-from-garbled-pdf) of embeded text *not* matching displayed text. This is problem, since most (all?) of the commonly available 
[PDF-to-text software tools]((https://en.wikipedia.org/wiki/List_of_PDF_software)) explicitly trust the source PDF file to contain embedded Unicode text strings that 
match the text that will be graphically displayed.

### Displayed &#x2260; embedded text string example: Microsoft Word
Using a modern version of Microsoft Word for MacOS (Version 16.105):
- Create a new document that contains a single word "difficult" in the "Aptos (Body)" font (which appears to be the default font circa early 2026)
- Save this document as a PDF file. 
- Open this newly created PDF document in the MacOS "Preview" PDF viewer and copy the displayed word "difficult" to the clipboard.
- Paste the clipboard contents into a new Microsoft Word document.
- Instead of "difficult", you will see "di#icult".
  
What just happedened? When creating the PDF file, MS Word replaced the two adjacent "f" characters in "difficult" with a single Unicode character representing the "&#xFB00;" ligature (where a [ligature](https://en.wikipedia.org/wiki/Ligature_(writing)) contains multiple symbols/characters in a single font glyph). However, rather than embedding a valid Unicode code for "&#xFB00;" (= 0xFB00), MS Word embedded the Unicode symbol "#" (= 0x0023).

Note that the choice to replace two characters "ff" with a single ligature character "&#xFB00;" is font dependent. If the above example is repeated using the "Times New Roman" font in MS Word, the resulting PDF file does *not* contain a ligature and the embedded text matches the displayed text (as expected).

### Displayed &#x2260; embedded text string example: Scientific literature
The final form for many (most?) scientific manuscripts is a PDF file. Scientific manuscripts often contain a mixture of many different Unicode symbol types (e.g., English, Greek, math symbols, etc.). When embedded characters don't match the displayed characters, the resulting extracted text can have dramatically different meaning. One example is when the displayed Greek symbol "&#x00B5;" (for micro) is assigned the Unicode value "m". When this change happens in units of concentration (i.e., "3.3 &#x00B5;M"), the resulting text extraction error (i.e., "3.3 mM" instead of "3.3 &#x00B5;M") yields a *drastically* different concentration! Since both "&#x00B5;M" (micromolar) and "mM" (milimolar) are valid units of concentration, this error can be difficult to detect. There are recent [research](https://pubmed.ncbi.nlm.nih.gov/40815276/) 
[papers](https://cebs.niehs.nih.gov/cebs/paper/16105) that use OCR to correct errors specifically related to concentration units, but do not provide a more general solution for the diverse set of Unicode symbols that commonly appear in scientifc PDF files.

### Poppler-science strategy for accurate Unicode symbol extraction
Poppler-science performs "per character" optical character recognition when extracting embedded text strings from PDF files. Unlike most existing pdf-to-text software tool, embedded Unicode values are not used. Instead each font glyph is internally rendered as a small bitmap image that is input to a multilayer perceptron algorithm to predict the corresponding Unicode value. Here are the details:
- The multilayer perceptron algorithm is only envoked when a new font glyph is encountered. Prediction results are stored in memory to allow fast lookup of Unicode values for font glyphs previously encountered in the current PDF file.
- The multilayer perceptron algorithm was trained by:
  - Extracting all font glyphs from XXX Open Access files downloaded from PubMed Central (PMC).
  - For each unique Unicode value, the set of PMC font glyphs were manually checked by visual inspection. Font glyphs that did not match Unicode value were excluded from the training set.
  - A two-layer perceptron, each layer with XXX and XXX nodes in each layer, was trained using Pytorch on an Apple M3 Mac Studio.
- The binary file of multilayer perceptron parameters (approximately 200 MB) are currently loaded from disk every time the Poppler-science pdftotext program is run.
- The inference of the Unicode value from an internal font glyph bitmap is implemented in C++ and performed using the CPU (using SIMD vector instructions). As a result, there is *no* dependancy on Pytorch software or GPU hardware.

## Superscript and subscript extraction
Many scientific PDF documents contain equations and/or technical names (e.g., H<sub>2</sub>O) with subscript and/or superscript text. Since most pdf-to-text applications group text into lines based on a shared baseline (i.e., the coordinate of the bottom of each letter), superscript text might appear on a line above and subscript text might appear on a line below. First, the spurious insertion of additional lines makes the resulting text more difficult to interpret. Second, when superscript/subscript text is displayed inline, the resulting concatination of regular text with superscript/subscript text can confound the identification and interpretation of names (i.e., the "[named entity recgonition](https://en.wikipedia.org/wiki/Named-entity_recognition)" problem). For example, naively extracting text from the scientific PDF [manuscript](https://pmc.ncbi.nlm.nih.gov/articles/PMC3384317/) displaying:

 ![text with superscripts and subscripts](examples/superscript_subscript.png)
 
 yields:

```
complex, four different samples containing 1.0 mM of the
complex in a 1:1.25 ratio were used (15N–Tfb1PH–
Rad2642–690, 15N/13C–Tfb1PH–Rad2642–690, 15N–Rad2642–
15
N/13C–Rad2642–690–Tfb1PH, respect690–Tfb1PH and
ively). All NMR experiments were carried out in 20 mM
```

where the superscript text string "15" appears on a line by itself, the subscript text, <sub>642-690</sub>, has been appended to the molecule name "Rad2", and text of the penultimate line is now out of order!

However, using the Poppler-science `pfttotext` to extract this same block of text yields:

```
complex, four different samples containing 1.0 mM of the
complex in a 1:1.25 ratio were used (<sup>15</sup>N–Tfb1PH–
Rad2<sub>642–690</sub>, <sup>15</sup>N/<sup>13</sup>C–Tfb1PH–Rad2<sub>642–690</sub>, <sup>15</sup>N–Rad2<sub>642–</sub>
<sub>690</sub>–Tfb1PH and <sup>15</sup>N/<sup>13</sup>C–Rad2<sub>642–690</sub>–Tfb1PH, respect-
ively). All NMR experiments were carried out in 20 mM
```

which outputs HTML tags to preserves the superscript and subscript structure of the source PDF file. Since the supscript and subscript HTML tags are valid Markdown, this text is easily displayed as: "complex, four different samples containing 1.0 mM of the
complex in a 1:1.25 ratio were used (<sup>15</sup>N–Tfb1PH–
Rad2<sub>642–690</sub>, <sup>15</sup>N/<sup>13</sup>C–Tfb1PH–Rad2<sub>642–690</sub>, <sup>15</sup>N–Rad2<sub>642–</sub>
<sub>690</sub>–Tfb1PH and <sup>15</sup>N/<sup>13</sup>C–Rad2<sub>642–690</sub>–Tfb1PH, respect-
ively). All NMR experiments were carried out in 20 mM"


## Document structure extraction

# How to build Poppler-science

# How to run Poppler-science: pdftotext