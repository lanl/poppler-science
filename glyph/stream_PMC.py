#!/usr/bin/env python3

import sys
import requests
import subprocess
import tempfile
import random
import time

def main():

    # The list of non-commercial PDF files from PubMedCentral, "oa_non_comm_use_pdf.txt"
    input_file_list = 'oa_non_comm_use_pdf.txt'

    # Allow the user to specify a different file of PDF files to process
    if len(sys.argv) > 1:
        input_file_list = sys.argv[1]

    completed_file_list = 'completed_pmc_pdf.txt'
    chunk_size = 100 # Process PDF files in groups of chunk_size

    output_prefix = 'font_db'
    output_suffix = 'bin.gz'

    completed_pdfs = set()

    # Read the completed PDF files (if any)
    try:
        fin = open(completed_file_list)

        for line in fin:

            line = line.strip()

            completed_pdfs.add(line)

        fin.close()

    except OSError:
        sys.stderr.write('Did not read any completed PDF files\n')

    # Read the PDF files to download
    try:
        fin = open(input_file_list)
    except OSError:
        sys.stderr.write('Unable to open {} for reading PDF file list\n'.format(input_file_list))
        sys.exit(0)
    
    target_files = list()

    line_number = 0
    num_skipped = 0

    for line in fin:

        line_number += 1

        if line_number == 1:
            continue # Skip the header
        
        # Skip PMC records that have been commented out (see the corrupted PDF in oa_pdf/04/1d/nihpp-2024.03.05.583638v2.PMC10942411.pdf)
        if line.find('#') == 0:
            print('Skipping record {}'.format(line), flush=True)
            continue

        data = line.split('\t')

        if len(data) != 5:
            print('Unable to parse {}'.format(data), flush=True)
        else:

            if data[0] in completed_pdfs:
                #print('Skipping completed pdf: {}'.format(data[0]))
                num_skipped += 1
            else:
                target_files.append(data[0])

    if num_skipped > 0:
        print('Skipped {} previously processed records'.format(num_skipped))

    # Randomize the PDF files to download
    random.shuffle(target_files)

    print('Found {} PDF files to process'.format(len(target_files)), flush=True)

    num_chunk = 0
    file_count = 0

    # A temporary directory for storing glyph bitmaps
    with tempfile.TemporaryDirectory(dir='.') as temp_dir:

        temp_pdf_file = temp_dir + '/stream_pmc.pdf'

        glyph_files = []
        completed_pdfs = []

        for f in target_files:

            print('{}: Extracting glyphs from {}'.format(file_count, f), flush=True)

            pdf_download_status = False

            for attempt in range(5):

                try:
                    url = 'https://ftp.ncbi.nlm.nih.gov/pub/pmc/' + f

                    response = requests.get(url, stream=True)
                    response.raise_for_status()  # Raise an exception for bad status codes (4xx or 5xx)

                    with open(temp_pdf_file, 'wb') as local_file:
                        for chunk in response.iter_content(chunk_size=8192):
                            local_file.write(chunk)

                    pdf_download_status = True
                    break # Success!

                except:
                    print('Unable to download {}'.format(f), flush=True)
                    time.sleep(3)
                    continue

            if not pdf_download_status:
                print('Giving up on downloading {}: attempt {}'.format(f, attempt), flush=True)
                continue

            temp_glyph_file = temp_dir + '/{}.{}'.format(len(glyph_files), output_suffix)
            glyph_files.append(temp_glyph_file)
            completed_pdfs.append(f)

            # Extract font glyphs from the temp file (and dumping the extractec text in /dev/null)
            # Note that the pdftotext program refers to the Poller-science version! There are many other
            # versions of this program and some of them may be in your execution path!
            cmd = ['pdftotext', '-ocr.dump_glyphs', temp_glyph_file, temp_pdf_file, '/dev/null']

            ret = subprocess.run(cmd, capture_output=True)
            
            if ret.returncode != 0:
                # Halt the streaming script, as this could identify a bug that needs to be fixed!
                sys.stderr.write('Unable to extract glyph information from {}\n'.format(f))
                sys.exit(0)
            
            file_count += 1
                
            if len(glyph_files) == chunk_size:

                output_file = '{}.{}.{}'.format(output_prefix, num_chunk, output_suffix)

                print('Collecting unique font glyphs in {}'.format(output_file), flush=True)

                # The unique_glyph program is part of the Poppler-science package
                cmd = ['unique_glyph', '-o', output_file, '--clean'] + glyph_files

                ret = subprocess.run(cmd, capture_output=False)
            
                if ret.returncode != 0:
                    sys.stderr.write('Unable to merge glyph bitmaps!\n')
                    sys.exit(0)
                    
                num_chunk += 1

                glyph_files = [] # Clear the current glyph file cache

                try:
                    fcomplete = open(completed_file_list, 'a')

                    for pdf in completed_pdfs:
                        print(pdf,file=fcomplete)

                    fcomplete.close()

                    completed_pdfs = [] # Clear the current completed file cache
                except OSError:
                    sys.stderr.write('Unable to open {} for writing completed PDF files\n'.format(completed_file_list))
                    sys.exit(0)
    
    print('Glyph extraction is complete!', flush=True)

main()