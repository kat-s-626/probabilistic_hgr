/*
 * Posterior Probability Computation
 * 
 * Computes normalized posterior probabilities from a list of likelihood values.
 * 
 * Given likelihoods P̂(ô | N^g_i, s_0) for i=1..k, computes:
 *   P(N^g_i | ô, s_0) = P̂(ô | N^g_i, s_0) / Σ_j P̂(ô | N^g_j, s_0)
 * 
 * Input format (CSV):
 *   hypothesis_name_1,likelihood_1
 *   hypothesis_name_2,likelihood_2
 *   ...
 * 
 * Output format (CSV):
 *   hypothesis_name_1,likelihood_1,posterior_1
 *   hypothesis_name_2,likelihood_2,posterior_2
 *   ...
 * 
 * Usage: ./compute_posterior_probabilities <input_file> <output_file>
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <cmath>

using namespace std;

struct HypothesisData {
    string name;
    double likelihood;
    double posterior;
};

int main(int argc, char** argv) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <input_file> <output_file>" << endl;
        cerr << endl;
        cerr << "Input format (CSV):" << endl;
        cerr << "  hypothesis_name,likelihood" << endl;
        cerr << endl;
        cerr << "Output format (CSV):" << endl;
        cerr << "  hypothesis_name,likelihood,posterior" << endl;
        return 1;
    }
    
    string inputFile = argv[1];
    string outputFile = argv[2];
    
    // Read input file
    vector<HypothesisData> hypotheses;
    ifstream inFile(inputFile);
    
    if (!inFile.is_open()) {
        cerr << "Error: Could not open input file: " << inputFile << endl;
        return 1;
    }
    
    string line;
    int lineNum = 0;
    while (getline(inFile, line)) {
        lineNum++;
        
        // Skip empty lines
        if (line.empty()) continue;
        
        // Parse CSV: hypothesis_name,likelihood
        size_t commaPos = line.find(',');
        if (commaPos == string::npos) {
            cerr << "Warning: Line " << lineNum << " does not contain comma separator, skipping" << endl;
            continue;
        }
        
        string name = line.substr(0, commaPos);
        string likelihoodStr = line.substr(commaPos + 1);
        
        // Parse likelihood value
        double likelihood;
        try {
            likelihood = stod(likelihoodStr);
        } catch (const exception& e) {
            cerr << "Warning: Line " << lineNum << " has invalid likelihood value: " << likelihoodStr << endl;
            continue;
        }
        
        // Check for non-negative likelihood
        if (likelihood < 0) {
            cerr << "Warning: Line " << lineNum << " has negative likelihood: " << likelihood << endl;
            continue;
        }
        
        HypothesisData data;
        data.name = name;
        data.likelihood = likelihood;
        data.posterior = 0.0;
        
        hypotheses.push_back(data);
    }
    
    inFile.close();
    
    if (hypotheses.empty()) {
        cerr << "Error: No valid hypotheses found in input file" << endl;
        return 1;
    }
    
    cout << "Read " << hypotheses.size() << " hypotheses from input file" << endl;
    
    // Compute sum of likelihoods
    double likelihoodSum = 0.0;
    for (const auto& h : hypotheses) {
        likelihoodSum += h.likelihood;
    }
    
    cout << "Likelihood sum: " << scientific << setprecision(10) << likelihoodSum << endl;
    
    // Check for zero sum
    if (likelihoodSum == 0.0) {
        cerr << "Error: Sum of likelihoods is zero. Cannot normalize." << endl;
        cerr << "All posteriors will be set to 0.0" << endl;
        
        // Write output with zero posteriors
        ofstream outFile(outputFile);
        if (!outFile.is_open()) {
            cerr << "Error: Could not open output file: " << outputFile << endl;
            return 1;
        }
        
        for (const auto& h : hypotheses) {
            outFile << h.name << "," 
                    << scientific << setprecision(10) << h.likelihood << ","
                    << "0.0" << endl;
        }
        
        outFile.close();
        return 1;
    }
    
    // Compute normalized posteriors
    double posteriorSum = 0.0;
    for (auto& h : hypotheses) {
        h.posterior = h.likelihood / likelihoodSum;
        posteriorSum += h.posterior;
    }
    
    cout << "Posterior sum: " << fixed << setprecision(10) << posteriorSum << endl;
    
    // Verify normalization (should be ~1.0)
    double normalizationError = abs(posteriorSum - 1.0);
    if (normalizationError > 1e-6) {
        cerr << "Warning: Posterior sum deviates from 1.0 by " << normalizationError << endl;
    } else {
        cout << "✓ Posteriors properly normalized" << endl;
    }
    
    // Write output file
    ofstream outFile(outputFile);
    if (!outFile.is_open()) {
        cerr << "Error: Could not open output file: " << outputFile << endl;
        return 1;
    }
    
    // Write CSV: hypothesis_name,likelihood,posterior
    for (const auto& h : hypotheses) {
        outFile << h.name << "," 
                << scientific << setprecision(10) << h.likelihood << ","
                << fixed << setprecision(10) << h.posterior << endl;
    }
    
    outFile.close();
    
    cout << "Results written to: " << outputFile << endl;
    
    return 0;
}
