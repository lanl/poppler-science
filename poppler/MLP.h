// © 2026. Triad National Security, LLC. All rights reserved.
// This program was produced under U.S. Government contract 89233218CNA000001 for Los Alamos National Laboratory (LANL), which 
// is operated by Triad National Security, LLC for the U.S. Department of Energy/National Nuclear Security Administration. 
// All rights in the program are reserved by Triad National Security, LLC, and the U.S. Department of Energy/National Nuclear 
// Security Administration. The Government is granted for itself and others acting on its behalf a nonexclusive, paid-up, 
// irrevocable worldwide license in this material to reproduce, prepare. derivative works, distribute copies to the public, 
// perform publicly and display publicly, and to permit others to do so.
// Copyright (C) 2026 Triad National Security, LLC; Jason Gans <jgans@lanl.gov>

#ifndef __MLP_CLASSIFICATION
#define __MLP_CLASSIFICATION

#include "glyph_classifier.h"
#include <vector>
#include <fstream> // Model serialization
#include <stdlib.h>

class MultiLayerPerceptron : public GlyphClassifier
{	
    public:
        typedef enum {RELU, SOFTMAX, LOGISTIC, UNKNOWN} ActivationFunction;
        
        typedef unsigned int PredictionIndex;
        typedef float ParamType;
	private:
        std::vector< std::vector< std::vector<ParamType> > > coefs; // Layer->Neuron->weight vectors
        std::vector< std::vector<ParamType> > intercepts; // Layer->Neuron->bias value
        std::vector<PredictionType> output_classes; // Uncode values
        ActivationFunction activation;
        ActivationFunction output_activation;

        std::vector<ParamType> softmax(const std::vector<ParamType> &m_x) const;
        std::vector<ParamType> logistic(const std::vector<ParamType> &m_x) const;
        std::vector<ParamType> relu(const std::vector<ParamType> &m_x) const;

        ActivationFunction str_to_function(const std::string &m_str) const;

	public:
	
        MultiLayerPerceptron()
        {
        };
              
        // Return the probability of each class
        std::unordered_map<PredictionType, float> predict(const Features &m_features) const;

        // Serialize the parameters of a trained model as binary data (from Scikit Learn)
        void load_param(const std::string &m_filename);
};

#endif // __MLP_CLASSIFICATION
