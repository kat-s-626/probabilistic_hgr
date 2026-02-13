/**
 * Compute Normalized Posterior Probabilities
 * 
 * Reads a file containing hypothesis names and their likelihoods,
 * then computes normalized posterior probabilities by dividing each
 * likelihood by the sum of all likelihoods.
 * 
 * Input file format (one line per hypothesis):
 *   hypothesis_name likelihood_value
 * 
 * Output file format:
 *   hypothesis_name likelihood_value posterior_value
 * 
 * Usage:
 *   ./compute_posterior <input_file> <output_file>
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <cmath>
#include <algorithm>

using namespace std;

struct HypothesisResult {
    string name;
    double likelihood;
    double posterior;
    
    HypothesisResult(const string& n, double l) 
        : name(n), likelihood(l), posterior(0.0) {}
};

int main(int argc, char** argv) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <input_file> <output_file>" << endl;
        cerr << endl;
        cerr << "Input file format (one per line):" << endl;
        cerr << "  hypothesis_name likelihood_value" << endl;
        cerr << endl;
        cerr << "Output file format:" << endl;
        cerr << "  hypothesis_name likelihood_value posterior_value" << endl;
        return 1;
    }
    
    string inputFile = argv[1];
    string outputFile = argv[2];
    
    // ========================================================================
    // Step 1: Read hypotheses and likelihoods from input file
    // ========================================================================
    
    vector<HypothesisResult> results;
    ifstream inFile(inputFile);
    
    if (!inFile.is_open()) {
        cerr << "Error: Cannot open input file: " << inputFile << endl;
        return 1;
    }
    
    string line;
    int lineNum = 0;
    while (getline(inFile, line)) {
        lineNum++;
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        istringstream iss(line);
        string hypName;
        double likelihood;
        
        if (!(iss >> hypName >> likelihood)) {
            cerr << "Warning: Skipping malformed line " << lineNum << ": " << line << endl;
            continue;
        }
        
        results.push_back(HypothesisResult(hypName, likelihood));
    }
    
    inFile.close();
    
    if (results.empty()) {
        cerr << "Error: No valid hypotheses found in input file" << endl;
        return 1;
    }
    
    cout << "Read " << results.size() << " hypotheses from " << inputFile << endl;
    
    // ========================================================================
    // Step 2: Compute sum of likelihoods
    // ========================================================================
    
    double likelihoodSum = 0.0;
    for (const auto& result : results) {
        likelihoodSum += result.likelihood;
    }
    
    cout << "Likelihood sum: " << scientific << setprecision(10) << likelihoodSum << endl;
    
    if (likelihoodSum == 0.0 || !isfinite(likelihoodSum)) {
        cerr << "Error: Invalid likelihood sum: " << likelihoodSum << endl;
        return 1;
    }
    
    // ========================================================================
    // Step 3: Compute normalized posteriors
    // ========================================================================
    
    for (auto& result : results) {
        result.posterior = result.likelihood / likelihoodSum;
    }
    
    // ========================================================================
    // Step 3.5: Sort results by posterior probability (descending)
    // ========================================================================
    
    sort(results.begin(), results.end(), 
         [](const HypothesisResult& a, const HypothesisResult& b) {
             return a.posterior > b.posterior;  // Descending order
         });
    
    cout << "Results sorted by posterior probability (descending)" << endl;
    
    // ========================================================================
    // Step 4: Write results to output file
    // ========================================================================
    
    ofstream outFile(outputFile);
    
    if (!outFile.is_open()) {
        cerr << "Error: Cannot open output file: " << outputFile << endl;
        return 1;
    }
    
    // Write header
    outFile << "# Normalized Posterior Probabilities" << endl;
    outFile << "# Format: hypothesis_name likelihood posterior" << endl;
    outFile << "# Likelihood sum: " << scientific << setprecision(10) << likelihoodSum << endl;
    outFile << "#" << endl;
    
    // Write results
    for (const auto& result : results) {
        outFile << result.name << " " 
                << scientific << setprecision(10) << result.likelihood << " "
                << scientific << setprecision(10) << result.posterior << endl;
    }
    
    outFile.close();
    
    cout << "Results written to " << outputFile << endl;
    
    // ========================================================================
    // Step 5: Display summary
    // ========================================================================
    
    cout << endl;
    cout << "Posterior Probabilities:" << endl;
    cout << "========================" << endl;
    
    double posteriorSum = 0.0;
    for (size_t i = 0; i < results.size(); i++) {
        cout << "Rank " << (i + 1) << ": " << results[i].name << endl;
        cout << "  Likelihood: " << scientific << setprecision(10) << results[i].likelihood << endl;
        cout << "  Posterior:  " << scientific << setprecision(10) << results[i].posterior << endl;
        cout << endl;
        
        posteriorSum += results[i].posterior;
    }
    
    cout << "Posterior sum: " << scientific << setprecision(10) << posteriorSum << endl;
    
    // Verify normalization (should sum to 1.0)
    double tolerance = 1e-6;
    if (abs(posteriorSum - 1.0) > tolerance) {
        cerr << "Warning: Posteriors do not sum to 1.0 (sum = " << posteriorSum << ")" << endl;
        return 1;
    } else {
        cout << "✓ Posteriors properly normalized (sum = 1.0)" << endl;
    }
    
    return 0;
}
