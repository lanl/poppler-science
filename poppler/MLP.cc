#include "MLP.h"
#include <math.h>
#include <fstream>
#include <zlib.h>
#include <algorithm>
#include <cstring> // For memcpy

using namespace std;

MultiLayerPerceptron::ActivationFunction MultiLayerPerceptron::str_to_function(const std::string &m_str) const
{
    if(m_str == "softmax"){
        return MultiLayerPerceptron::SOFTMAX;
    }
    
    if(m_str == "logistic"){
        return MultiLayerPerceptron::LOGISTIC;
    }

    if(m_str == "relu"){
        return MultiLayerPerceptron::RELU;
    }

    return MultiLayerPerceptron::UNKNOWN;
}

vector<MultiLayerPerceptron::ParamType> MultiLayerPerceptron::softmax(const vector<MultiLayerPerceptron::ParamType> &m_x) const
{
    vector<MultiLayerPerceptron::ParamType> ret(m_x);

    vector<MultiLayerPerceptron::ParamType>::const_iterator max_elem_iter = max_element( ret.begin(), ret.end() );

    if( max_elem_iter == ret.end() ){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::softmax: Unable to compute max element");
    }
    
    // Make a copy, since we are modifying the value pointed to by the iterator
    const MultiLayerPerceptron::ParamType max_value = *max_elem_iter;

    double norm = 0.0;

    for(vector<MultiLayerPerceptron::ParamType>::iterator i = ret.begin();i != ret.end();++i){

        *i = exp(*i - max_value);
        norm += *i;
    }

    if(norm > 0.0){
        norm = 1.0/norm;
    }

    for(vector<MultiLayerPerceptron::ParamType>::iterator i = ret.begin();i != ret.end();++i){
        *i = *i * norm;
    }

    return ret;
}

vector<MultiLayerPerceptron::ParamType> MultiLayerPerceptron::logistic(const vector<MultiLayerPerceptron::ParamType> &m_x) const
{
     vector<ParamType> ret(m_x);

    for(vector<MultiLayerPerceptron::ParamType>::iterator i = ret.begin();i != ret.end();++i){
        *i = 1.0/( 1.0 + exp( -(*i) ) );
    }

    return ret;
}

vector<MultiLayerPerceptron::ParamType> MultiLayerPerceptron::relu(const vector<MultiLayerPerceptron::ParamType> &m_x) const
{
    vector<ParamType> ret(m_x);

    for(vector<MultiLayerPerceptron::ParamType>::iterator i = ret.begin();i != ret.end();++i){
        *i = max(MultiLayerPerceptron::ParamType(0.0), *i);
    }

    return ret;
}

unordered_map<MultiLayerPerceptron::PredictionType, float> MultiLayerPerceptron::predict(const MultiLayerPerceptron::Features &m_features) const
{
    unordered_map<MultiLayerPerceptron::PredictionType, float> ret;

    const size_t num_layers = coefs.size();

    if(num_layers == 0){
        return ret;
    }

    const size_t num_features = m_features.size();

    if( num_features != coefs.front().front().size() ){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::predict: Number of features is different than the number of MLP coefficients");
    }

    vector<MultiLayerPerceptron::ParamType> x(num_features);

    for(size_t i = 0;i < num_features;++i){
        x[i] = MultiLayerPerceptron::ParamType(m_features[i])/255.0; // Scale the input feature values [0, 1]
    }

    // "coefs" tensor: Layer->2D weight matrix (next number of neurons x previous number of neurons)
    for(size_t layer = 0;layer < num_layers;++layer){

        const vector< vector<MultiLayerPerceptron::ParamType> > &W = coefs[layer];

        const size_t next_num_neurons = W.size();
        const size_t prev_num_neurons = W.front().size();

        vector<MultiLayerPerceptron::ParamType> next_x( intercepts[layer] );

        if(x.size() != prev_num_neurons){
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::predict: Size mismatch between previous number of neurons");
        }

        if(next_x.size() != next_num_neurons){
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::predict: Size mismatch between bias and weight coefficients");
        }

        for(size_t i = 0; i < next_num_neurons;++i){

            const vector<MultiLayerPerceptron::ParamType>& w = W[i];
            
            float sum = 0.0;

            // The following explicit request to the compiler to vectorize the following for loop
            // provides a very substantial speed up! Thanks to Claude 4.5 for suggesting.
            #pragma GCC ivdep
            #pragma clang loop vectorize(enable)
            for(size_t j = 0; j < prev_num_neurons;++j){
                sum += x[j] * w[j];
            }

            next_x[i] += sum; // next_x is initialized with the bias values, so we need to accumulate
        }

        MultiLayerPerceptron::ActivationFunction f = (layer == num_layers - 1) ? output_activation : activation;

        switch(f){
            case MultiLayerPerceptron::SOFTMAX:
                x = softmax(next_x);
                break;
            case MultiLayerPerceptron::RELU:
                x = relu(next_x);
                break;
            case MultiLayerPerceptron::LOGISTIC:
                x = logistic(next_x);
                break;
            default:
                error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::predict: Unknown activation function");
                break;
        };
    }

    for(size_t i = 0;i < x.size();++i){
        ret[ output_classes[i] ] = x[i];
    }

    return ret;
}

void MultiLayerPerceptron::load_param(const std::string &m_filename)
{
    gzFile fin = gzopen(m_filename.c_str(), "rb");
    
    if(fin == nullptr){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to open parameter file");
    }

    unsigned char* buffer = nullptr;
    unsigned char* ptr = nullptr;

    // Read the algorithm id and make sure that it is equal to GlyphClassifier::MULTILAYER_PERCEPTRON
    unsigned int algorithm_id;

    if(gzread(fin, &algorithm_id, sizeof(unsigned int)) != sizeof(unsigned int)){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read algorithm_id");
	}

    if(algorithm_id != GlyphClassifier::MULTILAYER_PERCEPTRON){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: algorithm id does not match GlyphClassifier::MULTILAYER_PERCEPTRON");
    }

    // Read the number of classes
    unsigned int num_classes;

    if(gzread(fin, &num_classes, sizeof(unsigned int)) != sizeof(unsigned int)){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read num_classes");
	}

    output_classes.resize(num_classes);

    buffer = new unsigned char[num_classes * sizeof(unsigned int)];

    if(buffer == nullptr){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to allocate buffer for output class array");
    }

    if( gzread(fin, buffer, num_classes * sizeof(unsigned int)) != int(num_classes * sizeof(unsigned int)) ){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read output class array");
	}

    ptr = buffer;

    for( unsigned int i = 0;i < num_classes;++i, ptr += sizeof(unsigned int) ){
        //output_classes[i] = *( (unsigned int*)(ptr) ); <-- generates a memory alignment warning.
        memcpy( &(output_classes[i]), ptr, sizeof(unsigned int) );
    }

    delete [] buffer;
    buffer = nullptr;

    unsigned int num_layers;

    if( gzread(fin, &num_layers, sizeof(unsigned int)) != sizeof(unsigned int) ){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read num_layers");
	}

    coefs.resize(num_layers);
    intercepts.resize(num_layers);

    for(unsigned int layer = 0;layer < num_layers;++layer){

        unsigned int prev_len;
        unsigned int next_len;

        if( gzread(fin, &next_len, sizeof(unsigned int)) != sizeof(unsigned int) ){
		    error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read next_len");
	    }

        if( gzread(fin, &prev_len, sizeof(unsigned int)) != sizeof(unsigned int) ){
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read prev_len");
	    }

        coefs[layer].resize(next_len);

        buffer = new unsigned char[prev_len * sizeof(float)];

        if(buffer == nullptr){
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to allocate buffer for coefficient row");
        }
        
        for(unsigned int i = 0;i < next_len;++i){
            
            if( gzread(fin, buffer, prev_len * sizeof(float)) != (prev_len * sizeof(float)) ){
                error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read coefficient row");
	        }

            vector<MultiLayerPerceptron::ParamType> &w = coefs[layer][i];

            w.resize(prev_len);

            ptr = buffer;

            for(unsigned int j = 0;j < prev_len;++j, ptr += sizeof(float)){
                //w[j] = *( (float*)ptr); <-- generates a memorys alignment warning
                memcpy( &(w[j]), ptr, sizeof(float));
            }
        }

        delete [] buffer;
        buffer = nullptr;
    }

    for(unsigned int layer = 0;layer < num_layers;++layer){

        unsigned int next_len;

        if( gzread(fin, &next_len, sizeof(unsigned int)) != sizeof(unsigned int) ){
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read next_len (2)");
	    }

        buffer = new unsigned char[next_len * sizeof(float)];

        if(buffer == nullptr){
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to allocate buffer for bias vector");
        }

        if( gzread(fin, buffer, next_len * sizeof(float)) != int(next_len * sizeof(float)) ){
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read bias vector");
	    }
        
        vector<MultiLayerPerceptron::ParamType> &bias = intercepts[layer];

        bias.resize(next_len);

        ptr = buffer;

        for(unsigned int i = 0;i < next_len;++i,ptr += sizeof(float)){
            //bias[i] = *( (float*)ptr ); <-- generates a memory alignment warning
            memcpy( &(bias[i]), ptr, sizeof(float) );
        }
        
        delete [] buffer;
        buffer = nullptr;
    }

    #define MAX_STR_LEN 128

    #define READ_STRING(X, LEN) \
        if( gzread(fin, &(LEN), sizeof(unsigned char)) != sizeof(unsigned char) ){ \
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read string length"); \
	    } \
        buffer = new unsigned char[ (LEN) * sizeof(unsigned char) ]; \
        if(buffer == nullptr){ \
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to allocate string buffer"); \
        } \
        if( gzread(fin, buffer, (LEN) * sizeof(unsigned char)) != int((LEN) * sizeof(unsigned char)) ){ \
            error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read string buffer"); \
	    } \
        X.assign( (char*)buffer, (char*)buffer + LEN); \
        delete [] buffer;
    
    // Read the activation function string
    string str_buffer;
    unsigned char str_len;

    READ_STRING(str_buffer, str_len);

    activation = str_to_function(str_buffer);

    if(activation == MultiLayerPerceptron::UNKNOWN){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read activation function string");
    }

    READ_STRING(str_buffer, str_len);

    output_activation = str_to_function(str_buffer);

    if(output_activation == MultiLayerPerceptron::UNKNOWN){
        error(errInternal, -1, __FILE__ ":MultiLayerPerceptron::load_param: Unable to read output activation function string");
    }

    if(fin){
        gzclose(fin);
        fin = nullptr;
    }
}