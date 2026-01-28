// Read glyph bitmaps (as gzipped compressed binary data) from input files and output a file containing the unique set of bitmaps
#include <stdlib.h>
#include <zlib.h>
#include <getopt.h>

#include <locale.h> // Needed for UTF-8 display in ncurses
#include <ncursesw/ncurses.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <limits>
#include <random>
#include "synonymous_glyphs.h" // Also contains definition of Unicode type

// For computing the Mahalnobis distance
#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>

using namespace std;

#define     SELECT_GLYPH_VERSION        "0.3 October 24, 2025"
// - For a given reported unicode value, use the Mahalnobis distance (i.e., z-score) to select font glyphs that are "close" to the average glyph.
// - Added a GUI to allow Mechanical Turk selection of correct unicode mappings
// - Added a checkpoint file to save user selections
// - Added borders to the rendered glyphs

//#define     SELECT_GLYPH_VERSION        "0.2 October 20, 2025"
// - Filter out glyphs with dominant unicode values in the private use areas (as defined by https://en.wikipedia.org/wiki/Private_Use_Areas)

//#define     SELECT_GLYPH_VERSION        "0.1 September 28, 2025"

#define     DEFAULT_MIN_GLYPH_COUNT         0       // Minimum number of publications to include a font
#define     DEFAULT_MIN_GLYPH_VARIANTS      10      // Minimum number of distinct glyph variants to include a Unicode value
#define     DEFAULT_SCALE_FACTOR            4       // Bitmap size reduction scale factor for accelerating Mahalnobis distance calculation

#define     SVD_THRESHOLD                   1.0e-7  // Singular value threshold for computing the SVD of glyph covariance matricies for Mahalnobis calculations

enum{
    GLYPH_DISTANCE,
    GLYPH_INDEX,
    GLYPH_HASH,
    GLYPH_VALID
};

// A quick, single glyph replacement for the deprecated C++ codecvt_utf8 API
// See the wikipedia page for a definition of UTF-8: https://en.wikipedia.org/wiki/UTF-8
// This functionality is most likely provided by UTF.h/UTF.cc ...
std::string utf8_to_string(Unicode m_code_point)
{
    std::string ret;

    ret.reserve(4);

    if(m_code_point < 0x80){ // One byte
        ret.push_back( char(m_code_point) );
    }
    else if(m_code_point < 0x800){ // Two byte
        ret.push_back( char( (3 << 6) | (m_code_point >> 6) ) );
        ret.push_back( char( (3 << 7) | (m_code_point & 0x3F) ) );
    }
    else if(m_code_point < 0x10000){ // Three byte
        ret.push_back( char( (7 << 5) | (m_code_point >> 12) ) );
        ret.push_back( char( (7 << 7) | ( (m_code_point >> 6) & 0x3F) ) );
        ret.push_back( char( (7 << 7) | (m_code_point & 0x3F) ) );
    }
    else{ // Four byte
        ret.push_back( char( (15 << 4) | (m_code_point >> 18) ) );
        ret.push_back( char( (15 << 7) | ( (m_code_point >> 12) & 0x3F) ) );
        ret.push_back( char( (15 << 7) | ( (m_code_point >> 6) & 0x3F) ) );
        ret.push_back( char( (15 << 7) | (m_code_point & 0x3F) ) );
    }

    return ret;
}

template <class T>
struct Bitmap : public std::vector<T>
{
    int width;
    int height;

    Bitmap()
    {
        clear();
    };

    Bitmap(const int m_width, const int m_height)
    {
        resize(m_width, m_height);
    };

    inline void clear()
    {
        width = height = 0;
        vector<T>::clear();
    };

    inline void resize(const int m_width, const int m_height)
    {
        width = m_width;
        height = m_height;

        if( (width < 0) || (height < 0)){
            throw __FILE__ ":Bitmap: Invalid width and/or height";
        }

        vector<T>::resize(width*height);
    };

    inline size_t size() const
    {
        return vector<T>::size();
    };

    inline T pixel(const int m_width_index, const int m_height_index) const
    {
        const int index = m_height_index*width + m_width_index;

        if( (index < 0) || ( index >= vector<T>::size() )){
            throw __FILE__ ":Bitmap::pixel: Index out of bounds";
        }

        return (*this)[index];
    };

    inline T& pixel(const int m_width_index, const int m_height_index)
    {
        const int index = m_height_index*width + m_width_index;

        if( (index < 0) || ( index >= vector<T>::size() )){
            throw __FILE__ ":Bitmap::pixel: Index out of bounds";
        }

        return (*this)[index];
    };

    enum {
        Q_MAX,
        Q_75,
        Q_50,
        Q_25,
        Q_MIN,
        NUM_Q
    };

    std::vector<T> quartiles() const
    {
        std::vector<T> ret(NUM_Q);

        std::vector<T> temp = (*this);

        std::sort(temp.begin(), temp.end());

        if(temp.empty() == false){

            // When assigning quartile values, recall that the pixel values are
            // sorted in ascending order
            ret[Q_MAX] = temp.back();

            // Most glyphs are dominated by whitespace (value = 0xFF = max). Don't include the 
            // maximum value when computing the 75%, 50% and 25% values
            int effecitve_len = temp.size();

            while( (effecitve_len > 0) && (temp[effecitve_len - 1] == ret[Q_MAX]) ){
                --effecitve_len;
            }

            ret[Q_75] = temp[effecitve_len*0.75];
            ret[Q_50] = temp[effecitve_len*0.50];
            ret[Q_25] = temp[effecitve_len*0.25];
            ret[Q_MIN] = temp.front();
        }

        return ret;
    };

    // Draw the glyph using the ncurses library
    void draw(int m_row, int m_col) const
    {
        // We have four different symbols to print with
        const string shade100 = utf8_to_string(0x2588);
        const string shade75 = utf8_to_string(0x2593);
        const string shade50 = utf8_to_string(0x2592);
        const string shade25 = utf8_to_string(0x2591);
        // The lowest value is a space
        const string shade0 = " ";

        const vector<T> q = quartiles();

        for(int r = 0;r < height;++r){
            for(int c = 0;c < width;++c){

                const T val = pixel(c, height - 1 - r);

                if(val == q[Q_MIN]){
                    mvprintw(m_row + r, m_col + c, "%s", shade100.c_str());
                    //cerr << val << " <-- Q_MIN" << endl;
                }
                else if(val == q[Q_MAX]){
                    mvprintw(m_row + r, m_col + c, "%s", shade0.c_str());
                    //cerr << val << " <-- Q_MAX" << endl;
                }
                else if(val <= q[Q_25]){
                    mvprintw(m_row + r, m_col + c, "%s", shade75.c_str());
                    //cerr << val << " <-- Q_25" << endl;
                }
                else if(val <= q[Q_50]){
                    mvprintw(m_row + r, m_col + c, "%s", shade50.c_str());
                    //cerr << val << " <-- Q_50" << endl;
                }
                else {//val >= q[Q_75]
                    mvprintw(m_row + r, m_col + c, "%s", shade25.c_str());
                    //cerr << val << " <-- Q_75" << endl;
                }
            }
        }
    };

    void zero()
    {
        for(typename std::vector<T>::iterator i = std::vector<T>::begin();i != std::vector<T>::end();++i){
            *i = 0;
        }
    };

    Bitmap<float> downscale(const int m_scale_factor) const
    {
        if(m_scale_factor <= 0){
            throw __FILE__ ":Bitmap::downscale: Invalid factor";
        }

        if( (height%m_scale_factor != 0) || (width%m_scale_factor != 0) ){
            throw __FILE__ ":Bitmap::downscale: height and width must be a multiple of scale factor";
        }

        int new_height = height/m_scale_factor;
        int new_width = width/m_scale_factor;

        const float norm = 1.0/(m_scale_factor*m_scale_factor);

        Bitmap<float> ret(new_width, new_height);

        ret.zero();

        for(int r = 0;r < height;++r){
            const int scaled_r = r/m_scale_factor;

            for(int c = 0;c < width;++c){
                const int scaled_c = c/m_scale_factor;

                ret.pixel(scaled_c, scaled_r) += pixel(c, r)*norm;
            }
        }

        return ret;
    };

    int glyph_height(const T &m_empty_pixel) const
    {
        int ret = height;

        for(int r = 0;r < height;++r){

            bool blank_row = true;

            for(int c = 0;c < width;++c){
                if(pixel(c, r) != m_empty_pixel){
                    blank_row = false;
                    break;
                }
            }

            if(blank_row == false){
                break;
            }

            ret -= 1;
        }

        for(int r = height - 1;r >= 0;--r){

            bool blank_row = true;

            for(int c = 0;c < width;++c){
                if(pixel(c, r) != m_empty_pixel){
                    blank_row = false;
                    break;
                }
            }

            if(blank_row == false){
                break;
            }

            ret -= 1;
        }

        return max(ret, 0);
    };

    int glyph_top(const T &m_empty_pixel) const
    {
        for(int r = 0;r < height;++r){

            bool blank_row = true;

            for(int c = 0;c < width;++c){
                if(pixel(c, r) != m_empty_pixel){
                    blank_row = false;
                    break;
                }
            }

            if(blank_row == false){
                return r;
            }
        }

        return height - 1;
    };

    int glyph_bottom(const T &m_empty_pixel) const
    {
        for(int r = height - 1;r >= 0;--r){

            bool blank_row = true;

            for(int c = 0;c < width;++c){
                if(pixel(c, r) != m_empty_pixel){
                    blank_row = false;
                    break;
                }
            }

            if(blank_row == false){
                return r;
            }
        }

        return 0;
    };

    int glyph_width(const T &m_empty_pixel) const
    {
        int ret = width;

        for(int c = 0;c < width;++c){

            bool blank_column = true;

            for(int r = 0;r < height;++r){
                if(pixel(c, r) != m_empty_pixel){
                    blank_column = false;
                    break;
                }
            }

            if(blank_column == false){
                break;
            }

            ret -= 1;
        }

        for(int c = width - 1;c >= 0;--c){

            bool blank_column = true;

            for(int r = 0;r < height;++r){
                if(pixel(c, r) != m_empty_pixel){
                    blank_column = false;
                    break;
                }
            }

            if(blank_column == false){
                break;
            }

            ret -= 1;
        }

        return max(ret, 0);
    };
};

// The std::hash<size_t>() is the identity hash (i.e., just returns the input)!
size_t hash_size_t(size_t m_obj)
{
    // From ChatGPT:
    // MurmurHash3 fmix64
    m_obj ^= m_obj >> 33; m_obj *= 0xff51afd7ed558ccdll;
    m_obj ^= m_obj >> 33; m_obj *= 0xc4ceb9fe1a85ec53ll;
    m_obj ^= m_obj >> 33;
    
    return m_obj;
}

template <class T>
std::size_t hash_function(const int m_width, const int m_height, const std::vector<T> &m_data)
{
    
    size_t ret = std::hash<int>()(m_width) ^ std::hash<int>()(m_height);

    size_t buffer = 0x0;
    size_t buffer_len = 0;

    for(typename std::vector<T>::const_iterator i = m_data.begin();i != m_data.end();++i){
        
        buffer = (buffer << 8*sizeof(T)) | *i;
        ++buffer_len;

        if( buffer_len == sizeof(buffer) ){

            // Include the index location since black and white images
            // will have many buffers that are all black or all white
            ret ^= hash_size_t(buffer + ( i - m_data.begin() ) );
            buffer = 0x0;
            buffer_len = 0;
        }
    }

    if(buffer_len > 0){

        ret ^= hash_size_t(buffer);
        buffer = 0x0;
        buffer_len = 0;
    }

    return ret;
}

namespace std {
    template <class T>
    struct hash< Bitmap<T> > {
        std::size_t operator()(const Bitmap<T>& m_obj) const {
            return hash_function(m_obj.width, m_obj.height, (vector<T>)m_obj);
        }
    };
};

class SortByIndexTable
{
    private:
        const unordered_map<size_t /*index*/, float /*height*/> *table_ptr;

    public:
        SortByIndexTable(const unordered_map<size_t /*index*/, float /*height*/> &m_table)
        {
            table_ptr = &m_table;
        };

        inline bool operator()(const tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> &m_a, tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> &m_b)
        {
            unordered_map<size_t /*index*/, float /*height*/>::const_iterator iter_a = table_ptr->find(get<GLYPH_INDEX>(m_a));

            if(iter_a == table_ptr->end()){
                throw __FILE__ ":: Unable to lookup table value by index";
            }

            unordered_map<size_t /*index*/, float /*height*/>::const_iterator iter_b = table_ptr->find(get<GLYPH_INDEX>(m_b));

            if(iter_b == table_ptr->end()){
                throw __FILE__ ":: Unable to lookup table value by index";
            }

            return iter_a->second < iter_b->second;
        };
};

typedef enum {
    BINARY_FORMAT,
    ASCII_FORMAT,
} FileFormat;

bool is_private_use(const Unicode m_u);
bool is_control_or_space(const Unicode m_u);
void write_checkpoint(const string &m_filename, const unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > > &m_clusters,
    const pair<Unicode, size_t/*hash*/> &m_current_glyph);
pair<size_t, size_t> restore_from_checkpoint(const string &m_filename, unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > > &m_clusters,
    pair<Unicode, size_t/*hash*/> &m_current_glyph);
void write_glyph_db(const string &m_filename, const  deque< pair< Bitmap<unsigned char>, Unicode> > &m_selected_db, 
    const unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > > &m_clusters);
bool parse_command(const string &m_command, const  deque< pair< Bitmap<unsigned char>, Unicode> > &m_selected_db,
    deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > &m_indicies, 
    const deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator &m_iter,
    Unicode &m_jump_to_cluster);

Bitmap<float> average_glyph(const deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > &m_indicies, 
    const deque< pair< Bitmap<unsigned char>, Unicode> > &m_glyph_db, const int m_width, const int m_height);

struct SortByGlyphDistance
{
    inline bool operator()(const std::tuple<float, size_t, size_t> &m_a, const std::tuple<float, size_t, size_t> &m_b) const
    {
        return get<0>(m_a) > get<0>(m_b); // Minimum heap <-- sort smallest distance to the top of the heap
    };
};

inline size_t update_cluster_index(const vector<size_t> &m_index_table, size_t m_index /*copy*/)
{
    while(m_index_table[m_index] != m_index){
        m_index = m_index_table[m_index];
    }

    return m_index;
};

int main(int argc, char* argv[])
{
    try{

        const size_t num_trial = 1000; // Only used for the compile-time optional confidence filtering
        const size_t update_checkpoint_every = 100; // Autosave the checkpoint file every 'update_checkpoint_every' glyph state changes
        const size_t jump_size = 10; // For fast scrolling through glyphs with up and down arrows

        const char* options = "o:?hz";
        int config_opt = 0;
        int long_index = 0;
        bool mask_glyph = false; // Use the glyph silhouette -- Approximately 1% classification improvement during (limited) testing

        struct option long_opts[] = {
            {"min-count", true, &config_opt, 1},
            {"mask", false, &config_opt, 2},
            {"min-variant", true, &config_opt, 3},
            {"check", true, &config_opt, 4},
            {"include", true, &config_opt, 5},
            {"z.scale", true, &config_opt, 8},
            {0,0,0,0} // Terminate options list
        };

        int opt_code;
        opterr = 0;

        bool print_usage = (argc == 1);
        deque<string> input_files;
        string output_file;
        string checkpoint_file;
        size_t min_glyph_count = DEFAULT_MIN_GLYPH_COUNT; // The minimum number of papers for a trusted association between a glyph and a unicode value
        size_t min_glyph_variants = DEFAULT_MIN_GLYPH_VARIANTS; // The minimum number of glyph variants required to include a unicode value
        float inclusion_fraction = 1.0;
        int z_scale_factor = DEFAULT_SCALE_FACTOR; // Bitmap size reduction scale factor for accelerating Mahalnobis distance calculation
        bool use_mahalnobis_score = false;

        while( (opt_code = getopt_long( argc, argv, options, long_opts, &long_index) ) != EOF ){

            switch( opt_code ){
                case 0:

                    if(config_opt == 1){ // min-count
                        min_glyph_count = abs( atoi(optarg) );
                        break;
                    }

                    if(config_opt == 2){ // mask
                        mask_glyph = true;
                        break;
                    }

                    if(config_opt == 3){ // min-variant
                        min_glyph_variants = abs( atoi(optarg) );
                        break;
                    }

                    if(config_opt == 4){ // check
                        checkpoint_file = optarg;
                        break;
                    }

                    if(config_opt == 5){ // include
                        inclusion_fraction = atof(optarg);
                        break;
                    }

                    if(config_opt == 8){ // z.scale
                        z_scale_factor = atoi(optarg);
                        break;
                    }

                    cerr << "Unknown command line flag!" << endl;
                    return EXIT_SUCCESS;
                case 'z':
                    use_mahalnobis_score = true;
                    break;
                case 'o':
                    output_file = optarg;
                    break;
                case 'h':
                case '?':
                    print_usage = true;
                    break;
                default:
                    cerr << '\"' << (char)opt_code << "\" is not a valid option!" << endl;
                    return EXIT_SUCCESS;
            };
        }

        for(int i = optind;i < argc;i++){
            input_files.push_back(argv[i]);
        }

        if(print_usage){

            cerr << "Select glyph version " << SELECT_GLYPH_VERSION << endl;
            cerr << "Usage:" << endl;
            cerr << "\t-o <output file of selected glyphs with sinlge unicode values>" << endl;
            cerr << "\t[-z (use Mahalanobis score, i.e., z-score, for glyph distance)]" << endl;
            cerr << "\t[--z.scale <integer scale factor> (glyph bitmap scale factor for accelerating distance calculation, Must be a factor of both width and height)] (default is "
                << DEFAULT_SCALE_FACTOR << ")" << endl;
            cerr << "\t[--min-count <minimum publication count for glyph inclusion>] (default is " << DEFAULT_MIN_GLYPH_COUNT << ")" << endl;
            cerr << "\t[--min-variant <minimum glyph variant count for unicode inclusion>] (default is " << DEFAULT_MIN_GLYPH_VARIANTS << ")" << endl;
            cerr << "\t[--mask (replace input glyphs with silhouettes)]" << endl;
            cerr << "\t[--include <fraction of glyphs to include per Unicode value>] (default is 1.0)" << endl;
            cerr << "\t--check <checkpoint file name for save selection>" << endl;
            cerr << "\t<input filename1> <input filename2> ..." << endl;

            return EXIT_SUCCESS;
        }

        if( output_file.empty() ){

            cerr << "Please specify an output filename for writing the selected glyphs(-o)" << endl;
            return EXIT_SUCCESS;
        }

        if( input_files.empty() ){

            cerr << "Please specify one or more input filenames" << endl;
            return EXIT_SUCCESS;
        }
        
        if( checkpoint_file.empty() ){

            cerr << "Please specify a checkpoint filename (--check)" << endl;
            return EXIT_SUCCESS;
        }

        if( (inclusion_fraction > 1.0) || (inclusion_fraction < 0.0) ){

            cerr << "Please specify the fraction of glyphs to include in the range [0, 1] (--include)" << endl;
            return EXIT_SUCCESS;
        }

        if(z_scale_factor <= 0){

            cerr << "Please specify an integer scaling factor > 0. Must be a factor of both height and width (--z.scale)" << endl;
            return EXIT_SUCCESS;
        }

        for(deque<string>::const_iterator file_iter = input_files.begin();file_iter != input_files.end();++file_iter){
            if(*file_iter == output_file){

                cerr << "Overwriting an input file with the output file of selected glyphs is not allowed!" << endl;
                return EXIT_FAILURE;
            }
        }

        if(mask_glyph){
            cerr << "Masking glyphs to create glyph silhouettes" << endl;
        }

        if(use_mahalnobis_score){
            cerr << "Using Mahalanobis score (i.e., z-score) for glyph distance ranking" << endl;
        }

        std::random_device rd;
        std::mt19937 rand(rd()); 
        std::uniform_real_distribution<float> rand_unit(0.0, 1.0);

        // Prevent GSL from aborting when it encounters an error
        gsl_set_error_handler_off();

        unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t/*count*/> > db;

        int global_width = -1; // All bitmaps must have the same dimensions
        int global_height = -1;

        for(deque<string>::const_iterator file_iter = input_files.begin();file_iter != input_files.end();++file_iter){

            cout << "Reading binary glyph data from " << *file_iter << endl;

            gzFile fin = gzopen(file_iter->c_str(), "rb");

            if(fin == NULL){

                cerr << "Unable to open " << *file_iter << " for reading binary data" << endl;
                return EXIT_FAILURE;
            }

            unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t/*count*/> > local_db;

            // Each binary glyph bitmap file contains zero or more bitmap records.
            // Each bitmap record contains:
            //  - unicode value (4 byte unsigned int)
            //  - Number of occurences of this unicode + bitmap combination    
            //  - bitmap_width (4 byte signed int)
            //  - bitmap_height (4 byte signed int)
            //  - bitmap_width*bitmap_height pixel value (each pixel is an unsigned char)

            while(true){ // Keep reading until we exhaust the available input data

                Unicode u;
                size_t glyph_count;
                int width;
                int height;
                unsigned char *buffer = NULL;

                int ret = gzread( fin, &u, sizeof(Unicode) );

                if(ret == 0){
                    break;
                }

                if( ret != sizeof(Unicode) ){
                    throw __FILE__ ": error reading unicode value";
                }

                if( gzread( fin, &glyph_count, sizeof(size_t) ) != sizeof(size_t) ){
                    throw __FILE__ ": error reading glyph_count";
                }

                if( gzread( fin, &width, sizeof(int) ) != sizeof(int) ){
                    throw __FILE__ ": error reading width";
                }

                if(width <= 0){
                    throw __FILE__ ": width <= 0";
                }

                if( gzread( fin, &height, sizeof(int) ) != sizeof(int) ){
                    throw __FILE__ ": error reading height";
                }

                if(height <= 0){
                    throw __FILE__ ": height <= 0";
                }

                if( (global_width < 0) || (global_height < 0) ){
                    global_width = width;
                    global_height = height;
                }
                else{
                    if(global_width != width){
                        throw __FILE__ ": global_width != width <-- all bitmaps must have the same width";
                    }

                    if(global_height != height){
                        throw __FILE__ ": global_height != height <-- all bitmaps must have the same height";
                    }
                }

                const size_t buffer_len = width*height;

                if( buffer_len > std::numeric_limits<unsigned int>::max() ){
                    throw __FILE__ ": buffer_len exceed maximum value of an unsigned int -- something strange has happened!";
                }

                buffer = new unsigned char[buffer_len];

                if(!buffer){
                    throw __FILE__ ": Unable to allocate bitmap buffer";
                }

                if( gzread(fin, buffer, sizeof(unsigned char)*buffer_len) != int(sizeof(unsigned char)*buffer_len) ){
                    throw __FILE__ ": error reading bitmap data";
                }

                if(mask_glyph){

                    for(size_t i = 0;i < buffer_len;++i){
                        //buffer[i] = (buffer[i] < 0xFF) ? 0x0 : 0xFF; // Binarize bitmaps to create silhouette
                        buffer[i] = (buffer[i] > 0x0) ? 0xFF : 0x0; // Binarize bitmaps to create silhouette
                    }
                }

                Bitmap<unsigned char> bmp(width, height);

                bmp.assign(buffer, buffer + buffer_len);

                if(buffer){

                    delete [] buffer;
                    buffer = NULL;
                }

                // Individual files may have repeated glyphs, avoid multiple-counting by
                // only storing the maximum count seen in a given file. The appearance of multiple glyphs per file is
                // a side-effect of the PDF parser saving unique glyphs per page (instead of per file). 
                size_t &count = local_db[bmp][u];

                count = max(glyph_count, count);
            }

            if(gzclose(fin) != Z_OK){
                throw __FILE__ ":main: gzclose(fin) != Z_OK";
            }

            // Accumulate the information from this file
            for(unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t> >::const_iterator i = local_db.begin();i != local_db.end();++i){

                unordered_map<Unicode, size_t>& dst = db[i->first];

                for(unordered_map<Unicode, size_t>::const_iterator j = i->second.begin();j != i->second.end();++j){
                    dst[j->first] += j->second;
                }
            }
        }

        const size_t num_pixels = global_width*global_height;

        cout << "Reading binary glyph data is complete" << endl;

        size_t num_single_map = 0;
        size_t num_high_confidence_multi_map = 0;
        size_t num_low_confidence_multi_map = 0;
        size_t num_control_or_space = 0;
        size_t num_low_count = 0;
        size_t num_blank_glyph = 0;

        #ifdef MERGE_SYNONYMOUS_GLPHS
        size_t num_synonymous = 0;
        #endif //MERGE_SYNONYMOUS_GLPHS

        size_t num_private_use = 0;
        size_t num_select = 0;
        Unicode min_selected_unicode_value = std::numeric_limits<Unicode>::max();
        Unicode max_selected_unicode_value = std::numeric_limits<Unicode>::min();
        unordered_set<Unicode> selected_unicode;
        deque< pair< Bitmap<unsigned char>, Unicode> > selected_db;
        deque< Bitmap<unsigned char> > not_selected_db;

        #ifdef MERGE_SYNONYMOUS_GLPHS
        // Merge synonymous glyphs
        for(unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t/*count*/> >::iterator i = db.begin();i != db.end();++i){

            bool has_synonyous_glyph = false;

            unordered_map<Unicode, size_t/*count*/> condensed;

            for(unordered_map<Unicode, size_t/*count*/>::const_iterator j = i->second.begin();j != i->second.end();++j){
                
                unordered_map<Unicode /*redundant*/, Unicode /*canonical value*/>::const_iterator iter = equivalent_glyphs.find(j->first);

                if( iter != equivalent_glyphs.end() ){
                    
                    has_synonyous_glyph = true;
                    condensed[iter->second] += j->second;
                }
                else{
                    condensed[j->first] += j->second;
                }
            }

            if(has_synonyous_glyph){

                ++num_synonymous;
                i->second = condensed;
            }
        }

        cout << "Merged synonymous glyphs" << endl;
        #endif // MERGE_SYNONYMOUS_GLPHS

        for(unordered_map< Bitmap<unsigned char>, unordered_map<Unicode, size_t/*count*/> >::iterator i = db.begin();i != db.end();++i){
        
            bool is_blank = true;

            for(Bitmap<unsigned char>::const_iterator j = i->first.begin();(j != i->first.end()) && is_blank;++j){
                if(*j != 0xFF){
                    is_blank = false;
                }
            }

            num_blank_glyph += is_blank;

            deque< pair<size_t, Unicode> > rank;

            for(unordered_map<Unicode, size_t>::const_iterator j = i->second.begin();j != i->second.end();++j){
                rank.push_back( make_pair(j->second, j->first) );
            }

            sort( rank.begin(), rank.end() );

            // Skip glyphs for which the most common unicode value is a control or space character
            if( is_control_or_space(rank.back().second) ){

                ++num_control_or_space;
                not_selected_db.push_back(i->first);
                continue;
            }

            if( is_private_use(rank.back().second) ){

                ++num_private_use;
                not_selected_db.push_back(i->first);
                continue;
            }

            // Skip glyphs have not been observed at least min_glyph_count times
            if(rank.back().first < min_glyph_count){

                ++num_low_count;
                not_selected_db.push_back(i->first);
                continue;
            }

            if(rank.size() == 1){
                ++num_single_map;
            }
            else{ // Glyphs with more than one associated unicode value
     
                // Recompute the ranking without control or space unicode values
                rank.clear();
                size_t norm = 0;

                for(unordered_map<Unicode, size_t>::const_iterator j = i->second.begin();j != i->second.end();++j){
                    
                    if( !is_control_or_space(j->first) && !is_private_use(j->first) ){

                        rank.push_back( make_pair(j->second, j->first) );
                        norm += j->second;
                    }
                }

                sort( rank.begin(), rank.end() );

                if(rank.size() == 1){

                    // Counts as a single map glyph even though we removed contol/space unicode values
                    ++num_single_map;
                }
                #ifdef CONFIDENCE_FILTER
                else{ // Glyphs with more than one associated unicode value (that is not a space or a control character!)

                    const size_t num_glyph = rank.size();

                    // For multimap glyphs, assign as high or low confidence base on the probability of observing a different most-abundant unicode value
                    // by chance. The number of trials sets a probability upper bound (i.e. p-value).
                    vector<float> cumulative_p; // <-- cumulative glyph probability
                    cumulative_p.reserve(num_glyph);

                    float tmp = 0.0;

                    for(deque< pair<size_t, Unicode> >::const_reverse_iterator j = rank.rbegin();j != rank.rend();++j){

                        const float p = float(j->first)/norm;
                        tmp += p;

                        cumulative_p.push_back(tmp);
                    }

                    bool high_confidence_glyph = true;

                    for(size_t trial = 0;(trial < num_trial) && high_confidence_glyph;++trial){

                        vector<size_t> count(num_glyph);

                        for(size_t n = 0;n < norm;++n){
                            
                            vector<float>::const_iterator iter = cumulative_p.end();

                            while( iter == cumulative_p.end() ){

                                const float r = rand_unit(rand);

                                iter = upper_bound(cumulative_p.begin(), cumulative_p.end(), r);
                            }

                            ++count[iter - cumulative_p.begin()];
                        }

                        for(size_t j = 1;j < num_glyph;++j){
                            if(count[j] >= count[0]){
                                high_confidence_glyph = false;
                            }
                        }
                    }

                    if(high_confidence_glyph){
                        ++num_high_confidence_multi_map;
                    }
                    else{

                        ++num_low_confidence_multi_map;

                        // DEBUG
                        //for(deque< pair<size_t, Unicode> >::const_reverse_iterator j = rank.rbegin();j != rank.rend();++j){
                        //    cout << "'" << utf8_to_string(j->second) << "'(0x" << std::hex << j->second << std::dec << ") <-- " << j->first 
                        //        << (iscntrl(j->second) ? ", cntrl" : "") << (isspace(j->second) ? ", space" : "") << endl;
                        //}
                        not_selected_db.push_back(i->first);
                        continue; // Skip low confidence  glyphs
                    }
                }
                #endif // CONFIDENCE_FILTER
            }

            ++num_select;

            min_selected_unicode_value = min(min_selected_unicode_value, rank.back().second);
            max_selected_unicode_value = max(max_selected_unicode_value, rank.back().second);
            selected_unicode.insert(rank.back().second);

            selected_db.push_back( make_pair(i->first, rank.back().second) );
        }

        cout << "Found a total of " << db.size() << " input glyphs" << endl;

        #ifdef MERGE_SYNONYMOUS_GLPHS
        cout << "Found " << num_synonymous << " glyphs with one or more non-cannonical unicode values" << endl;
        #endif // MERGE_SYNONYMOUS_GLPHS

        cout << "Found " << num_low_count << " low count glyphs (< " << min_glyph_count << " publications; not selected)" << endl;
        cout << "Found " << num_private_use << " private use glyphs (not selected)" << endl;
        cout << "Found " << num_single_map << " single-mapped glyphs (selected)" << endl;
        cout << "Found " << num_blank_glyph << " blank glyphs (not selected)" << endl;
        cout << "Found " << num_control_or_space << " space or control labeled glyphs (not selected)" << endl;
        cout << "Found " << num_high_confidence_multi_map << " multi-mapped high confidence glyph labels (selected)" << endl;
        cout << "Found " << num_low_confidence_multi_map << " multi-mapped low confidence glyph labels (not selected)" << endl;
        cout << "Selected a total of " << num_select << " font glyphs, each with a single associated unicode value" << endl;
        cout << selected_unicode.size() << " unique selected unicode values range from 0x" << std::hex << min_selected_unicode_value << " to 0x" << max_selected_unicode_value << std::dec << endl;
        
        db.clear(); // Clear the original glyph information (we will work with selected_db and not_selected_db)
        
        // Cluster the glyphs
        // We can't use single linkage clustering, as it requires computing O(N^2) pairwise distnaces which won't scale to >10^5 glyphs. 
        // Instead, use a contrained variant of k-means to cluster glyphs

        // Deterine the initial conditions by grouping the glyphs by unicode value (even though some of these assignments will be incorrect)
        unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > > clusters;

        for(size_t i = 0;i < num_select;++i){
            clusters[selected_db[i].second].push_back( make_tuple(0.0 /*distance is not set yet*/, i, 0x0 /*hash not set*/, true) );
        }

        cout << "Preliminary clusters assigned to " << clusters.size() << " unicode values" << endl;

        // The number of glyphs per cluster
        deque< pair<size_t, Unicode> > glyph_abundance;

        for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > >::const_iterator i = clusters.begin();i != clusters.end();++i){
            glyph_abundance.push_back( make_pair(i->second.size(), i->first) );
        }

        sort(glyph_abundance.begin(), glyph_abundance.end());

        deque<Unicode> ordered_glyphs;

        for(deque< pair<size_t, Unicode> >::const_iterator i = glyph_abundance.begin();i != glyph_abundance.end();++i){

            // DEBUG
            //cout << "'" << utf8_to_string(i->second) << "' (0x" << std::hex << i->second << std::dec << ") has " << i->first << " gylphs";

            // Remove glyphs that only have a few variants
            if(i->first < min_glyph_variants){
                
                clusters.erase(i->second);
                // DEBUG
                //cout << " *excluded*" <<endl;
            }
            else{
                ordered_glyphs.push_back(i->second);
            }
        }

        cout << "Initial clusters assigned to " << clusters.size() << " unicode values after removing Unicode values with < " 
            << min_glyph_variants << " glyph variants" << endl;

        cout << "Computing hash values ... ";
        cout.flush();

        for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > >::iterator i = clusters.begin();i != clusters.end();++i){

            for(deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> >::iterator j = i->second.begin();j != i->second.end();++j){
                get<GLYPH_HASH>(*j) = hash< Bitmap<unsigned char> >()(selected_db[get<GLYPH_INDEX>(*j)].first);
            }
        }

        cout << "done." << endl;

        // Restore glyph classifications and distances from checkpoint
        pair<Unicode, size_t/*hash*/> checkpoint_glyph = make_pair(0x0, 0x0);
        const pair<size_t /*num matched*/, size_t /*num checkpoint values*/> checkpoint_status = restore_from_checkpoint(checkpoint_file, clusters, checkpoint_glyph);

        cout << "Computing glyph distances ... ";
        cout.flush();

        if(use_mahalnobis_score){

            // Enable parallel calculation of the Mahalnobis distance for all glyphs
            const size_t num_cluster = clusters.size();

            vector<Unicode> u;

            u.reserve(num_cluster);

            for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > >::iterator i = clusters.begin();i != clusters.end();++i){
                u.push_back(i->first);
            }

            #pragma omp parallel for
            for(size_t i = 0;i < num_cluster;++i){

                unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > >::iterator cluster_iter = clusters.find(u[i]);
                
                if( cluster_iter == clusters.end() ){
                    throw __FILE__ ":main: Unable to lookup cluster by unicode";
                }

                Bitmap<float> ave = average_glyph(cluster_iter->second,  selected_db, global_width, global_height).downscale(z_scale_factor);

                const size_t num_pixels = ave.size();
                const size_t num_glyph = cluster_iter->second.size();

                // Pack the glyph bitmaps into a single matrix. Remove the average.
                gsl_matrix* X = gsl_matrix_alloc(num_pixels, num_glyph); // <-- Columns are glyphs, rows are pixel values

                if(X == NULL){
                    throw __FILE__ ":main: Unable to allocate data matrix";
                }

                for(size_t j = 0;j < num_glyph;++j){

                    const tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> &index = cluster_iter->second[j];
                    const Bitmap<float> tmp = selected_db[get<GLYPH_INDEX>(index)].first.downscale(z_scale_factor);
                
                    for(size_t k = 0;k < num_pixels;++k){
                        
                        const float delta = tmp[k]/255.0 - ave[k]; // Scale pixel values to [0,1] and subtract the average

                        gsl_matrix_set(X, k, j, delta); // Store mean-centered glyph data in columns
                    }
                }

                gsl_matrix* cov = gsl_matrix_alloc(num_pixels, num_pixels);

                if(cov == NULL){
                    throw __FILE__ ":main: Unable to allocate covariance matrix";
                }

                gsl_blas_dgemm(CblasNoTrans, CblasTrans, 1.0 / (num_glyph - 1.0), X, X, 0.0, cov);

                // Use singular value decomposition to compute the pseudo inverse of the comvariance matrix
                gsl_matrix* V = gsl_matrix_alloc(num_pixels, num_pixels);
                gsl_vector* S = gsl_vector_alloc(num_pixels);
                gsl_vector* work = gsl_vector_alloc(num_pixels);
                
                const int status = gsl_linalg_SV_decomp(cov, V, S, work);

                if(status == 0){ // Valid SVD
                    
                    // Step 4: Compute y = Σ⁺ * (x - μ)
                    // Σ⁺ = V * S⁺ * U^T
                    // where S⁺_i = 1/S_i for nonzero S_i
                    for(size_t j = 0;j < num_glyph;++j){

                        gsl_vector_view delta = gsl_matrix_column(X, j); // mean-centered glyph j
                        gsl_vector *y = gsl_vector_calloc(num_pixels);

                        for(size_t k = 0;k < num_pixels;++k) {

                            double sk = gsl_vector_get(S, k);

                            if (sk > SVD_THRESHOLD) {

                                gsl_vector_view Uk = gsl_matrix_column(cov, k);
                                gsl_vector_view Vk = gsl_matrix_column(V, k);

                                double proj;

                                gsl_blas_ddot(&Uk.vector, &delta.vector, &proj);

                                gsl_blas_daxpy(proj/sk, &Vk.vector, y);
                            }
                        }

                        // Step 5: d^2 = diff^T * y
                        double sqdist;

                        gsl_blas_ddot(&delta.vector, y, &sqdist);
                        sqdist = sqrt(sqdist);
                        
                        get<GLYPH_DISTANCE>(cluster_iter->second[j]) = sqdist;

                        gsl_vector_free(y);
                    }
                }
                else{ // status != 0; SVD failed

                    cerr << "Unable to compute SVD for '" << utf8_to_string(u[i]) << "'; falling back to vanilla distance" << endl;

                    // SVD failed, so fall back to vanilla distance calculation
                    for(size_t j = 0;j < num_glyph;++j){

                        gsl_vector_view delta = gsl_matrix_column(X, j); // mean-centered glyph j
                        
                        double sqdist;

                        gsl_blas_ddot(&delta.vector, &delta.vector, &sqdist);
                        sqdist = sqrt(sqdist);
                        
                        get<GLYPH_DISTANCE>(cluster_iter->second[j]) = sqdist;
                    }
                }

                gsl_matrix_free(X);
                gsl_matrix_free(cov);
                gsl_matrix_free(V);
                gsl_vector_free(S);
                gsl_vector_free(work);
            }
        }
        else{

            for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > >::iterator i = clusters.begin();i != clusters.end();++i){

                const Bitmap<float> ave = average_glyph(i->second,  selected_db, global_width, global_height);
                
                // Compute the distance of each glyph to the cluster center
                for(deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*included?*/> >::iterator j = i->second.begin();j != i->second.end();++j){

                    float d = 0.0;

                    for(size_t m = 0;m < num_pixels;++m){
                            
                        const float delta = ave[m] - selected_db[get<GLYPH_INDEX>(*j)].first[m]/255.0;

                        d += delta*delta;
                    }

                    d = sqrt(d);

                    get<GLYPH_DISTANCE>(*j) = d;
                }
            }
        }

        // Sort the glyphs by distance and apply any inclusion-based classification
        for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > >::iterator i = clusters.begin();i != clusters.end();++i){

            sort( i->second.begin(), i->second.end() );

            // Only apply the include criteria if we are not restoring from a checkpoint
            if ( (inclusion_fraction < 1.0) && (checkpoint_status.second == 0)){

                const size_t num_glyph = i->second.size();
                const size_t begin = inclusion_fraction * num_glyph;

                for(size_t j = begin;j < num_glyph;++j){
                    get<GLYPH_VALID>(i->second[j]) = false;
                }
            }
        }

        cout << "done." << endl;

        // Initialize NCURSES
        //int screen_row, screen_col;

        setlocale(LC_ALL, ""); // Needed for UTF-8 codepoint display with ncurses
        initscr();
        
        //getmaxyx(stdscr, screen_row, screen_col);
        keypad(stdscr, TRUE); // For access to arrow keys
        mousemask(ALL_MOUSE_EVENTS, NULL);
        //mouseinterval(0); // Reduce the lag in responding to mouse events <-- breaks double-click detection
        noecho();

        if(checkpoint_status.second > 0){

            mvprintw(0, 0, "Restored %zu glyph states out of %zu stored glyph states", checkpoint_status.first, checkpoint_status.second);
            getch();
        }

        deque<Unicode>::const_reverse_iterator glyph_iter = ordered_glyphs.rbegin();

        if(checkpoint_glyph.first != 0x0){
            while( (glyph_iter != ordered_glyphs.rend()) && (*glyph_iter != checkpoint_glyph.first) ){
                ++glyph_iter;
            }
        }

        while(true){ // Loop until the user quits

            unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > >::iterator i = clusters.find(*glyph_iter);

            if( i == clusters.end() ){
                throw __FILE__ ":main: Unable to lookup cluster by glyph";
            }

            Bitmap<float> ave = average_glyph(i->second,  selected_db, global_width, global_height);

            // Display glyphs to the user for Mechanical Turk classification
            deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator j = i->second.rbegin();

            if(checkpoint_glyph.first != 0x0){ // Are we restoring from a previous checkpoint for the first time?

                while ( (j != i->second.rend() ) && (get<GLYPH_HASH>(*j) != checkpoint_glyph.second) ){
                    ++j;
                }

                checkpoint_glyph = make_pair(0x0, 0x0);
            }

            bool quit = false;
            bool next_cluster = true; // Should we advance to the next cluster? If not, we will return to the previous cluster
            Unicode jump_to_cluster = 0x0; // Should we jump to the specified cluster?
            size_t num_glyph_state_changes = 0;

            while(true){

                if(num_glyph_state_changes >= update_checkpoint_every){

                    write_checkpoint( checkpoint_file, clusters, make_pair(i->first, get<GLYPH_HASH>(*j)) );
                    num_glyph_state_changes = 0;
                }

                // Make room to draw borders
                const int top_glyph_offset = 2;
                
                // Render the current cluster label and glyph index
                mvprintw(0, 0, "Cluster (%ld of %zu) glyph is '%s' (0x%X): %ld of %zu: distance = %f", ( glyph_iter - ordered_glyphs.rbegin() ) + 1, 
                    ordered_glyphs.size(), utf8_to_string(i->first).c_str(), i->first, ( j - i->second.rbegin() ) + 1, i->second.size(), get<GLYPH_DISTANCE>(*j));

                // Render the current glyph on the left and the average glyph on the right
                if( j != i->second.rend() ){
                    selected_db[get<GLYPH_INDEX>(*j)].first.draw(top_glyph_offset, 1);
                }

                if(get<GLYPH_VALID>(*j) == false){

                    // Show the glyph in not valid by drawing a large "X" through the glyph
                    for(int k = 0;k < min(global_width, global_height);++k){
                        mvprintw(top_glyph_offset + k, k + 1, "\\");
                        mvprintw(top_glyph_offset + global_height - 1 - k, k + 1, "/");
                    }
                }

                ave.draw(top_glyph_offset, global_width + 1);

                const string horizontal_border = utf8_to_string(0x2500);
                const string vertical_border = utf8_to_string(0x2502);

                // Render the top border
                mvprintw(1, 0, "%s", utf8_to_string(0x250C).c_str());

                for(int i = 1;i <= global_width;++i){
                    mvprintw(1, i, "%s", horizontal_border.c_str());
                }

                mvprintw(1, global_width + 1, "%s", utf8_to_string(0x252C).c_str());

                for(int i = 2 + global_width;i < (2*global_width + 2);++i){
                    mvprintw(1, i, "%s", horizontal_border.c_str());
                }

                mvprintw(1, 2*global_width + 2, "%s", utf8_to_string(0x2510).c_str());

                // Render the bottom border
                mvprintw(1 + global_height, 0, "%s", utf8_to_string(0x2514).c_str());

                for(int i = 1;i <= global_width;++i){
                    mvprintw(1 + global_height, i, "%s", horizontal_border.c_str());
                }

                mvprintw(1 + global_height, global_width + 1, "%s", utf8_to_string(0x2534).c_str());

                for(int i = 2 + global_width;i < (2*global_width + 2);++i){
                    mvprintw(1 + global_height, i, "%s", horizontal_border.c_str());
                }

                mvprintw(1 + global_height, 2*global_width + 2, "%s", utf8_to_string(0x2518).c_str());

                // Render the left border
                for(int i = 2;i <= global_height;++i){
                    mvprintw(i, 0, "%s", vertical_border.c_str());
                }

                // Render the middle border
                for(int i = 2;i <= global_height;++i){
                    mvprintw(i, global_width + 1, "%s", vertical_border.c_str());
                }

                // Render the right border
                for(int i = 2;i <= global_height;++i){
                    mvprintw(i, 2*global_width + 2, "%s", vertical_border.c_str());
                }

                refresh();

                const int c = getch();
                bool done_with_cluster = false;
                MEVENT mouse_event;
                char str_buffer[256];

                switch(c){
                    case KEY_MOUSE:
                        if(getmouse(&mouse_event) == OK){	/* When the user clicks left mouse button */

                            // Did the user click on the average glyph?
                            mouse_event.x -= global_width + 2;
                            mouse_event.y -= top_glyph_offset;

                            if( (mouse_event.y >= 0) && (mouse_event.y < global_width) &&
                                (mouse_event.x >= 0) && (mouse_event.x < global_height) ){
                                
                                if(mouse_event.bstate & BUTTON1_DOUBLE_CLICKED){
                                    
                                    size_t count = 0;

                                    // Remove any glyphs with non-0xFF pixels in the selected coordiantes
                                    for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::iterator k = i->second.begin();k != i->second.end();++k){

                                        if(get<GLYPH_VALID>(*k) == false){
                                            continue;
                                        }

                                        if(selected_db[get<GLYPH_INDEX>(*k)].first.pixel(mouse_event.x, global_height - 1 - mouse_event.y) != 0xFF){
                                            get<GLYPH_VALID>(*k) = false;
                                            ++count;
                                        }
                                    }

                                    // Recompute the average glyph to reflect the change in glyph inclusion
                                    ave = average_glyph(i->second,  selected_db, global_width, global_height);
                                }
                            }
                        }
                        break;
                    case KEY_HOME:
                        // Jump to the first glyph
                        j = i->second.rbegin();
                        break;
                    case KEY_END:
                        // Jump to the last glyph
                        j = i->second.rend() - 1;
                        break;
                    case KEY_DOWN:
                        // Next glyph x jump size
                        for(size_t k = 0;k < jump_size;++k){
                            if( (j + 1) != i->second.rend() ){
                                ++j;
                            }
                        }
                        break;
                    case KEY_RIGHT:
                        // Next glyph
                        if( (j + 1) != i->second.rend() ){
                            ++j;
                        }
                        break;
                    case KEY_UP:
                        // Previous glyph x jump size
                        for(size_t k = 0;k < jump_size;++k){
                            if( j != i->second.rbegin() ){
                                --j;
                            }
                        }
                        break;
                    case KEY_LEFT:
                        // Previous glyph
                        if( j != i->second.rbegin() ){
                            --j;
                        }
                        break;
                    case ' ': // space
                        // Toggle the valid state of the current glyph
                        get<GLYPH_VALID>(*j) = !get<GLYPH_VALID>(*j);
                        ++num_glyph_state_changes;

                        // Recompute the average glyph to reflect the change in glyph inclusion
                        ave = average_glyph(i->second,  selected_db, global_width, global_height);
                        break;
                    case 'c': // Save checkpoint file
                        write_checkpoint(checkpoint_file, clusters, make_pair(i->first, get<GLYPH_HASH>(*j)));
                        num_glyph_state_changes = 0;
                        break;
                    case 'w':
                        // Provide a warning (as writing can take a while)
                        mvprintw(1, 0, "Writing font database to %s ... ", output_file.c_str());
                        refresh();
                        write_glyph_db(output_file, selected_db, clusters);
                        break;
                    case 'q':
                        quit = true;
                        write_checkpoint(checkpoint_file, clusters, make_pair(i->first, get<GLYPH_HASH>(*j)));
                        break;
                    case '/':
                        mvprintw(1, 0, "Command: ");
                        refresh();
                        echo();
                        getstr(str_buffer);
                        noecho();
                        
                        if(parse_command(str_buffer, selected_db, i->second, j, jump_to_cluster) ){
                        
                            // Recompute the average glyph to reflect the change in glyph inclusion
                            ave = average_glyph(i->second,  selected_db, global_width, global_height);
                        }

                        if(jump_to_cluster != 0x0){

                            done_with_cluster = true;
                            next_cluster = false;
                        }

                        break;
                    case KEY_ENTER:
                        // Done with current cluster
                        done_with_cluster = true;
                        break;
                    case KEY_NPAGE: // Page-down
                        next_cluster = true;
                        done_with_cluster = true;
                        break;
                    case KEY_PPAGE: // Page-up
                        next_cluster = false;
                        done_with_cluster = true;
                        break;
                };

                if(done_with_cluster || quit){
                    break;
                }
            }

            if(quit){
                break;
            }

            if(jump_to_cluster)
            {
                for(deque<Unicode>::const_reverse_iterator j = ordered_glyphs.rbegin();j != ordered_glyphs.rend();++j){
                    if(jump_to_cluster == *j){

                        glyph_iter = j;
                        break;
                    }
                }
            }
            else{
                if(next_cluster){
                    if( (glyph_iter + 1) != ordered_glyphs.rend() ){
                        ++glyph_iter;
                    }
                }
                else{
                    if(glyph_iter != ordered_glyphs.rbegin()){
                        --glyph_iter;
                    }
                }
            }
        }

        endwin();

        cout << "Glyph selection is complete" << endl;
        write_glyph_db(output_file, selected_db, clusters);
    }
    catch(const char* error){
        cerr << "Caught the error: " << error << endl;
        return EXIT_FAILURE;
    }
    catch(...){
        cerr << "Caught an unhandled error" << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

bool is_private_use(const Unicode m_u)
{
    // Is this unicode value within one of the three Unicode private use ranges?
    // See: https://en.wikipedia.org/wiki/Private_Use_Areas
    return ( ( (m_u >= 0xE000) && (m_u <= 0xF8FF) ) ||
        ( (m_u >= 0xF0000) && (m_u <= 0xFFFFD) ) ||
        ( (m_u >= 0x100000) && (m_u <= 0x10FFFD) ) );
}

bool is_control_or_space(const Unicode m_u)
{
    if( iscntrl(m_u) || isspace(m_u) ){
        return true;
    }

    // See: C1 block in https://en.wikipedia.org/wiki/List_of_Unicode_characters
    // Not sure why the iscntrl() does not return true for these values.
    if( (m_u >= 0x80) && (m_u <= 0x9F) ){
        return true;
    }

    // See: https://en.wikipedia.org/wiki/Specials_(Unicode_block)
    if( (m_u >= 0xFFF0) && (m_u <= 0xFFFF) ){
        return true;
    }

    return false;
}

void write_checkpoint(const string &m_filename, const unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > > &m_clusters,
    const pair<Unicode, size_t/*hash*/> &m_current_glyph)
{
    const string tmp_file = m_filename + ".temp";

    ofstream fout(tmp_file.c_str(), ios::binary);

    if(!fout){
        throw __FILE__ ":write_checkpoint: Unable to write checkpoint file!";
    }

    fout.write((char*)&m_current_glyph.first, sizeof(Unicode)); // Current unicode value
    fout.write((char*)&m_current_glyph.second, sizeof(size_t)); // Current glyph hash

    for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > >::const_iterator i = m_clusters.begin();i != m_clusters.end();++i){

        fout.write((char*)&(i->first), sizeof(Unicode));

        const size_t num_hash = i->second.size();
        size_t num_invalid_hash = 0;
        size_t* hash_values = new size_t[num_hash];

        if(hash_values == NULL){
            throw __FILE__ ":write_checkpoint: Unable to allocate hash value array";
        }

         for(deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> >::const_iterator j = i->second.begin();j != i->second.end();++j){

            // Only store invalid glyphs
            if( !get<GLYPH_VALID>(*j) ){

                hash_values[num_invalid_hash] = get<GLYPH_HASH>(*j);
                ++num_invalid_hash;
            }
        }

        fout.write((char*)&(num_invalid_hash), sizeof(size_t));
        fout.write((char*)(hash_values), num_invalid_hash*sizeof(size_t));

        delete [] hash_values;
    }

    fout.close();

    // Overwrite the old checkpoint file (if present) with the new checkpoint file
    if( rename(tmp_file.c_str(), m_filename.c_str()) != 0){
        throw __FILE__ ":write_checkpoint: Unable to rename checkpoint file!";
    }
}

// Return the number of matched glyphs and the total number of glyphs stored in the checkpoint file
pair<size_t, size_t> restore_from_checkpoint(const string &m_filename, unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > > &m_clusters,
    pair<Unicode, size_t/*hash*/> &m_current_glyph)
{
    pair<size_t, size_t> ret = make_pair(0, 0);

    ifstream fin(m_filename.c_str(), ios::binary);

    if(!fin){
        return ret;
    }

    // Restore the current unicode value and glyph hash
    if( !fin.read((char*)&m_current_glyph.first, sizeof(Unicode)) ){
        throw __FILE__ ":restore_from_checkpoint: Unable to read current unicode value";
    }

    if( !fin.read((char*)&m_current_glyph.second, sizeof(size_t)) ){
        throw __FILE__ ":restore_from_checkpoint: Unable to read current glyph hash value";
    }

    unordered_map<Unicode, unordered_set<size_t> /*hash value*/> previous;

    while(true){

        Unicode u;

        if( !fin.read((char*)&u, sizeof(Unicode))){
            break;
        }

        size_t num_hash = 0;

        if( !fin.read((char*)&num_hash, sizeof(size_t))){
            throw __FILE__ ":restore_from_checkpoint: Unable to restore from checkpoint (1)";
        }

        size_t* hash_values = new size_t[num_hash];

        if(hash_values == NULL){
            throw __FILE__ ":restore_from_checkpoint: Unable to allocate hash value array";
        }

        if( !fin.read((char*)hash_values, num_hash*sizeof(size_t))){
            throw __FILE__ ":restore_from_checkpoint: Unable to restore from checkpoint (2)";
        }

        unordered_set<size_t> &ref = previous[u];

        for(size_t i = 0;i < num_hash;++i){
            ref.insert(hash_values[i]);
        }

        delete [] hash_values;

        ret.second += num_hash;
    }

    for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > >::iterator i = m_clusters.begin();i != m_clusters.end();++i){

        unordered_map<Unicode, unordered_set<size_t> /*hash value*/>::const_iterator iter = previous.find(i->first);

        if( iter == previous.end() ){
            continue;
        }

        for(deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> >::iterator j = i->second.begin();j != i->second.end();++j){

            if( iter->second.find(get<GLYPH_HASH>(*j)) != iter->second.end() ){
                
                get<GLYPH_VALID>(*j) = false;
                ++ret.first;
            }
        }
    }

    return ret;
}

void write_glyph_db(const string &m_filename, const deque< pair< Bitmap<unsigned char>, Unicode> > &m_selected_db, 
    const unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > > &m_clusters)
{

    const string tmp_file = m_filename + ".temp";
    
    gzFile fout = gzopen(tmp_file.c_str(), "wb");

    if(!fout){
        throw __FILE__ ":write_glyph_db: Unable to open output file for writing";
    }
    
    for(unordered_map<Unicode, deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> > >::const_iterator i = m_clusters.begin();i != m_clusters.end(); ++i){

        for(deque< tuple<float /*distance*/, size_t /*glyph index*/, size_t /*hash*/, bool /*valid?*/> >::const_iterator j = i->second.begin();j != i->second.end();++j){

            if( !get<GLYPH_VALID>(*j) ){
                continue;
            }

            const pair< Bitmap<unsigned char>, Unicode>& ref = m_selected_db[get<GLYPH_INDEX>(*j)];

            // Each bitmap record contains:
            //  - unicode value (4 byte unsigned int)
            //  - Number of occurences of this unicode + bitmap combination    
            //  - bitmap_width (4 byte signed int)
            //  - bitmap_height (4 byte signed int)
            //  - bitmap_width*bitmap_height pixel value (each pixel is an unsigned char)

            int ret = gzwrite( fout, &(i->first), sizeof(Unicode) );

            if( ret != sizeof(Unicode) ){
                throw __FILE__ ":write_glyph_db: error writing unicode value";
            }

            size_t num_glyph = 1;

            if( gzwrite( fout, &(num_glyph), sizeof(size_t) ) != sizeof(size_t) ){
                throw __FILE__ ":write_glyph_db: error writing glyph_count";
            }

            if( gzwrite( fout, &(ref.first.width), sizeof(int) ) != sizeof(int) ){
                throw __FILE__ ":write_glyph_db: error writing width";
            }

            if( gzwrite( fout, &(ref.first.height), sizeof(int) ) != sizeof(int) ){
                throw __FILE__ ":write_glyph_db: error writing height";
            }

            if( gzwrite(fout, (void*)ref.first.data(), sizeof(unsigned char)*ref.first.size()) != int(sizeof(unsigned char)*ref.first.size()) ){
                throw __FILE__ ":write_glyph_db: error writing bitmap data";
            }
        }
    }

    gzclose(fout);
    fout = NULL;

    // Overwrite the old output file (if present) with the new output file
    if( rename(tmp_file.c_str(), m_filename.c_str()) != 0){
        throw __FILE__ ":write_glyph_db: Unable to rename output file!";
    }
}

Bitmap<float> average_glyph(const deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > &m_indicies, 
    const deque< pair< Bitmap<unsigned char>, Unicode> > &m_glyph_db, const int m_width, const int m_height)
{
    const size_t num_pixels = m_width*m_height;

    Bitmap<float> ave(m_width, m_height);

    float norm = 0.0;

    for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*included?*/> >::const_iterator j = m_indicies.begin();j != m_indicies.end();++j){

        if( get<GLYPH_VALID>(*j) ){

            for(size_t k = 0;k < num_pixels;++k){
                ave[k] += m_glyph_db[get<GLYPH_INDEX>(*j)].first[k]/255.0; /* Scale input pixel values to [0,1]*/
            }

            norm += 1.0;
        }
    }

    norm  = (norm <= 0.0) ? 0.0 : 1.0/norm;

    for(size_t k = 0;k < num_pixels;++k){
        ave[k] *= norm;
    }

    return ave;
}

// Return true if the average glyph needs to be recomputed
bool parse_command(const string &m_command, const  deque< pair< Bitmap<unsigned char>, Unicode> > &m_selected_db,
    deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> > &m_indicies, 
    const deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator &m_iter,
    Unicode &m_jump_to_cluster)
{
    if(m_command == "<-a->"){ // Activate all glyphs

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::iterator i = m_indicies.begin();i != m_indicies.end();++i){
            get<GLYPH_VALID>(*i) = true;
        }

        return true;
    }

    if(m_command == "<-a"){ // Activate previous glyphs

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_indicies.rbegin();i != m_iter;++i){
            get<GLYPH_VALID>(*i) = true;
        }

        return true;
    }

    if(m_command == "a->"){ // Activate following glyphs

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_iter;i != m_indicies.rend();++i){
            get<GLYPH_VALID>(*i) = true;
        }

        return true;
    }

    if(m_command == "<-d->"){ // Deactivate all glyphs

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::iterator i = m_indicies.begin();i != m_indicies.end();++i){
            get<GLYPH_VALID>(*i) = false;
        }

        return true;
    }

    if(m_command == "<-d"){ // Deactivate previous glyphs

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_indicies.rbegin();i != m_iter;++i){
            get<GLYPH_VALID>(*i) = false;
        }

        return true;
    }

    if(m_command == "d->"){ // Deactivate following glyphs

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_iter;i != m_indicies.rend();++i){
            get<GLYPH_VALID>(*i) = false;
        }

        return true;
    }

    if(m_command == "current->"){ // Resort the following glyphs by similarity to the current glyph

        const Bitmap<unsigned char> &ref = m_selected_db[get<GLYPH_INDEX>(*m_iter)].first;
        const size_t num_pixels = ref.size();
        unordered_map<size_t /*index*/, float /*height*/> distance_table;

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_iter + 1;i != m_indicies.rend();++i){
            
            float d = 0.0;

            for(size_t m = 0;m < num_pixels;++m){
                    
                const float delta = ref[m]/255.0 - m_selected_db[get<GLYPH_INDEX>(*i)].first[m]/255.0;

                d += delta*delta;
            }

            d = sqrt(d);

            distance_table[get<GLYPH_INDEX>(*i)] = d;
        }

        // Use the SortByIndexTable and a separate table of distances to avoid clobbering the expensive Mahalanobis distances (if used)
        sort(m_iter + 1,  m_indicies.rend(), SortByIndexTable(distance_table));

        return true;
    }
    
    if(m_command == "ave->"){ // Resort the following glyphs by distance to the average glyph

        // Recall that reverse_iterator::base() is a forward iterator that points to the "next" element
        // that will be visited.
        if( m_iter.base() != m_indicies.begin() ){
            sort(m_indicies.begin(), m_iter.base() - 1);
        }

        return true;
    }

    if(m_command == "h->"){ // Resort the following glyphs by height

        unordered_map<size_t /*index*/, float /*height*/> height_table;

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_iter + 1;i != m_indicies.rend();++i){
            height_table[get<GLYPH_INDEX>(*i)] = m_selected_db[get<GLYPH_INDEX>(*i)].first.glyph_height(0xFF /*empty pixel value*/);
        }

        sort( m_iter + 1,  m_indicies.rend(), SortByIndexTable(height_table) );

        return true;
    }

    if(m_command == "w->"){ // Resort the following glyphs by width

        unordered_map<size_t /*index*/, float /*height*/> width_table;

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_iter + 1;i != m_indicies.rend();++i){
            width_table[get<GLYPH_INDEX>(*i)] = m_selected_db[get<GLYPH_INDEX>(*i)].first.glyph_width(0xFF /*empty pixel value*/);
        }

        sort( m_iter + 1,  m_indicies.rend(), SortByIndexTable(width_table) );

        return true;
    }

    if(m_command == "b->"){ // Resort the following glyphs by the location of the bottom of each glyph

        unordered_map<size_t /*index*/, float /*height*/> bottom_table;

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_iter + 1;i != m_indicies.rend();++i){
            bottom_table[get<GLYPH_INDEX>(*i)] = m_selected_db[get<GLYPH_INDEX>(*i)].first.glyph_top(0xFF /*empty pixel value*/); // Glyphs are flipped, so return glyph_top
        }

        sort( m_iter + 1,  m_indicies.rend(), SortByIndexTable(bottom_table) );

        return true;
    }

    if(m_command == "t->"){ // Resort the following glyphs by the location of the top of each glyph

        unordered_map<size_t /*index*/, float /*height*/> top_table;

        for(deque< tuple<float /*distance*/, size_t /*index*/, size_t /*hash*/, bool /*valid?*/> >::reverse_iterator i = m_iter + 1;i != m_indicies.rend();++i){
            top_table[get<GLYPH_INDEX>(*i)] = m_selected_db[get<GLYPH_INDEX>(*i)].first.glyph_bottom(0xFF /*empty pixel value*/); // Glyphs are flipped, so return glyph_bottom
        }

        sort( m_iter + 1,  m_indicies.rend(), SortByIndexTable(top_table) );

        return true;
    }

    if(m_command.find("0x") == 0){

        m_jump_to_cluster = std::stoul(m_command, nullptr, 16);
        return false;
    }

    return false;
}
