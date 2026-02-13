/**
 * Compute Normalized Likelihood for HTN Goal Recognition
 * 
 * Implements: P̂(ô | N^g, s_0) ≈ P̃(ô, π^+, N^+ | N^g, s_0) / P̃(N_base, π_base | N^g, s_0)
 * 
 * Where:
 *   - Numerator: Most probable execution that embeds observations ô
 *   - Denominator: Most probable unconstrained execution (baseline)
 * 
 * This normalization accounts for the "cost difference" intuition:
 * Goals are more likely when the observation-consistent execution is nearly as
 * probable as the baseline execution.
 * 
 * Usage: ./compute_normalized_likelihood <model.psas> <observation_plan_log> <baseline_plan_log> [alpha] [num_obs] [full_obs] [p_det]
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include "htnModel/Model.h"

using namespace std;
using namespace progression;

// ============================================================================
// UTILITY FUNCTIONS (from compute_full_likelihood.cpp)
// ============================================================================

// Parse plan from log file
vector<string> parsePlanFromLog(const string& logFile) {
    vector<string> plan;
    ifstream file(logFile);
    if (!file.is_open()) {
        cerr << "Error: Cannot open log file: " << logFile << endl;
        return plan;
    }

    string line;
    bool inPlanSection = false;
    
    while (getline(file, line)) {
        if (line.find("==>") != string::npos) {
            inPlanSection = true;
            continue;
        }
        
        if (line.find("<==") != string::npos || line.find("root ") == 0) {
            break;
        }
        
        if (inPlanSection) {
            if (line.find("<abs>") != string::npos || line.find("->") != string::npos) {
                continue;
            }
            
            size_t spacePos = line.find(' ');
            if (spacePos != string::npos) {
                string actionPart = line.substr(spacePos + 1);
                size_t start = actionPart.find_first_not_of(" \t");
                size_t end = actionPart.find_last_not_of(" \t\r\n");
                if (start != string::npos && end != string::npos) {
                    string action = actionPart.substr(start, end - start + 1);
                    if (action.find("->") == string::npos && !action.empty()) {
                        plan.push_back(action);
                    }
                }
            }
        }
    }
    return plan;
}

// Find task ID by name
int findTaskId(Model* htn, const string& name) {
    for (int i = 0; i < htn->numTasks; i++) {
        if (htn->taskNames[i] == name) return i;
    }
    // Try lowercase
    string lower = name;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (int i = 0; i < htn->numTasks; i++) {
        string taskLower = htn->taskNames[i];
        transform(taskLower.begin(), taskLower.end(), taskLower.begin(), ::tolower);
        if (taskLower == lower) return i;
    }
    return -1;
}

// Helper to find method ID by name and decomposed task
int findMethodId(Model* htn, const string& methodName, int taskId) {
    for (int m = 0; m < htn->numMethods; m++) {
        if (htn->decomposedTask[m] == taskId && htn->methodNames[m] == methodName) {
            return m;
        }
    }
    return -1;
}

// Check if action is applicable in state
bool isApplicable(Model* htn, const unordered_set<int>& state, int action) {
    for (int i = 0; i < htn->numPrecs[action]; i++) {
        if (state.find(htn->precLists[action][i]) == state.end()) {
            return false;
        }
    }
    return true;
}

// Apply action to state
void applyAction(Model* htn, unordered_set<int>& state, int action) {
    // Delete effects
    for (int i = 0; i < htn->numDels[action]; i++) {
        state.erase(htn->delLists[action][i]);
    }
    // Add effects
    for (int i = 0; i < htn->numAdds[action]; i++) {
        state.insert(htn->addLists[action][i]);
    }
}

// Parse decomposition tree from planner log to get task-method pairs actually used
// Returns map of task name -> number of alternative methods for that task
// Also populates usedMethodIds with the actual method IDs that were used
map<string, int> parseDecompositionTreeFromLog(const string& logFile, Model* htn, set<int>* usedMethodIds = nullptr) {
    map<string, int> taskMethodCounts;
    map<int, vector<int>> taskToMethods;
    
    // First, build the full task-to-methods mapping from the model
    for (int m = 0; m < htn->numMethods; m++) {
        int taskId = htn->decomposedTask[m];
        taskToMethods[taskId].push_back(m);
    }
    
    ifstream file(logFile);
    if (!file.is_open()) {
        cerr << "Warning: Cannot open log file: " << logFile << endl;
        return taskMethodCounts;
    }
    
    string line;
    bool inDecompTree = false;
    
    while (getline(file, line)) {
        // Look for start of decomposition tree
        if (line.find("root 0") != string::npos) {
            inDecompTree = true;
            continue;
        }
        
        // Look for end of decomposition tree
        if (inDecompTree && line.find("<==") != string::npos) {
            break;
        }
        
        // Parse decomposition lines: "ID TASK -> METHOD ..."
        if (inDecompTree && line.find(" -> ") != string::npos) {
            // Extract task name (between first space and " -> ")
            size_t firstSpace = line.find(' ');
            size_t arrow = line.find(" -> ");
            
            if (firstSpace != string::npos && arrow != string::npos && arrow > firstSpace) {
                string taskName = line.substr(firstSpace + 1, arrow - firstSpace - 1);
                
                // Skip abstract action markers and method preconditions
                if (taskName.find("<abs>") == 0 || taskName.find("__method_precondition") == 0) {
                    continue;
                }
                
                // Extract method name (after " -> ")
                size_t methodStart = arrow + 4;
                size_t methodEnd = line.find(' ', methodStart);
                if (methodEnd == string::npos) methodEnd = line.length();
                string methodName = line.substr(methodStart, methodEnd - methodStart);
                
                // Find this task in the model to get the number of alternative methods
                int taskId = findTaskId(htn, taskName);
                if (taskId >= 0 && taskToMethods.find(taskId) != taskToMethods.end()) {
                    int numMethods = taskToMethods[taskId].size();
                    // Only count compound tasks (that have methods), not primitive actions
                    if (numMethods > 0) {
                        taskMethodCounts[taskName] = numMethods;
                        
                        // Track which method was actually used
                        if (usedMethodIds != nullptr) {
                            int usedMethodId = findMethodId(htn, methodName, taskId);
                            if (usedMethodId >= 0) {
                                usedMethodIds->insert(usedMethodId);
                            }
                        }
                    }
                }
            }
        }
    }


    file.close();
    return taskMethodCounts;
}

// Get methods per task (for Stage I) - OLD VERSION, kept for compatibility
map<int, vector<int>> getMethodsPerTask(Model* htn) {
    map<int, vector<int>> taskToMethods;
    for (int m = 0; m < htn->numMethods; m++) {
        int taskId = htn->decomposedTask[m];
        taskToMethods[taskId].push_back(m);
    }
    return taskToMethods;
}

// ============================================================================
// STAGE I: NETWORK DECOMPOSITION PROBABILITY P(N | N^g)
// ============================================================================

double computeStage1Probability(const map<string, int>& taskMethodCounts, bool verbose = true) {
    double logProb = 0.0;
    int numCompoundTasks = 0;
    
    if (verbose) {
        cout << "\n=== STAGE I: Network Decomposition ===" << endl;
        cout << "Using uniform method selection: P(m|X) = 1/|M(X)|" << endl;
        cout << endl;
    }
    
    for (const auto& entry : taskMethodCounts) {
        string taskName = entry.first;
        int numMethods = entry.second;
        double prob = 1.0;
        if (numMethods == 0) {
            continue;
        }
        prob = 1.0 / numMethods;
        
        if (verbose) {
            cout << "  Task: " << taskName
                 << " | |M(X)| = " << numMethods 
                 << " | P(m|X) = " << prob << endl;
        }
        
        logProb += log(prob);
        numCompoundTasks++;
    }
    
    double stage1_prob = exp(logProb);
    
    if (verbose) {
        cout << "\nCompound tasks with methods: " << numCompoundTasks << endl;
        cout << "log P(N | N^g) = " << logProb << endl;
        cout << "P(N | N^g) = " << stage1_prob << endl;
    }
    
    return stage1_prob;
}

// ============================================================================
// STAGE II: EXECUTABLE LINEARIZATION PROBABILITY P(π | N, s_0)
// ============================================================================

set<pair<int,int>> extractOrderingConstraints(Model* htn, const set<int>* methodFilter = nullptr) {
    set<pair<int,int>> orderings;
    
    for (int m = 0; m < htn->numMethods; m++) {
        // Skip methods not in the filter if a filter is provided
        if (methodFilter != nullptr && methodFilter->find(m) == methodFilter->end()) {
            continue;
        }
        
        for (int i = 0; i < htn->numOrderings[m]; i += 2) {
            int beforeIdx = htn->ordering[m][i];
            int afterIdx = htn->ordering[m][i + 1];
            int beforeTask = htn->subTasks[m][beforeIdx];
            int afterTask = htn->subTasks[m][afterIdx];
            orderings.insert({beforeTask, afterTask});
        }
    }
    
    // Compute transitive closure
    bool changed = true;
    while (changed) {
        changed = false;
        set<pair<int,int>> newPairs;
        
        for (const auto& p1 : orderings) {
            for (const auto& p2 : orderings) {
                if (p1.second == p2.first) {
                    pair<int,int> newPair = {p1.first, p2.second};
                    if (orderings.find(newPair) == orderings.end()) {
                        newPairs.insert(newPair);
                        changed = true;
                    }
                }
            }
        }
        
        for (const auto& p : newPairs) {
            orderings.insert(p);
        }
    }
    
    return orderings;
}

double computeStage2Probability(Model* htn, const vector<int>& plan,
                                const set<pair<int,int>>& orderingConstraints,
                                bool verbose = true) {
    
    if (verbose) {
        cout << "\n=== STAGE II: Executable Linearization ===" << endl;
        cout << "Computing available sets based on ordering constraints" << endl;
        cout << "Ordering constraints: " << orderingConstraints.size() << endl;
        cout << endl;
    }
    
    // Initialize state from s0
    unordered_set<int> currentState;
    for (int i = 0; i < htn->s0Size; i++) {
        currentState.insert(htn->s0List[i]);
    }
    
    set<int> executed;
    set<int> remaining;
    for (int taskId : plan) {
        if (taskId >= 0 && taskId < htn->numActions) {
            remaining.insert(taskId);
        }
    }
    
    double logProb = 0.0;
    
    for (size_t t = 0; t < plan.size(); t++) {
        int selectedAction = plan[t];
        
        // Find available actions at this step
        set<int> availableActions;
        
        for (int taskId : remaining) {
            // Check 1: Minimal w.r.t. partial order (no unexecuted predecessors)
            bool hasUnexecutedPredecessor = false;
            for (const auto& ord : orderingConstraints) {
                if (ord.second == taskId && remaining.find(ord.first) != remaining.end()) {
                    hasUnexecutedPredecessor = true;
                    break;
                }
            }
            
            // Check 2: Applicable in current state
            bool applicable = isApplicable(htn, currentState, taskId);
            
            if (!hasUnexecutedPredecessor && applicable) {
                availableActions.insert(taskId);
            }
        }
        
        int applicableCount = availableActions.size();
        if (applicableCount == 0) applicableCount = 1; // Avoid division by zero
        
        double stepProb = 1.0 / applicableCount;
        logProb += log(stepProb);
        
        if (verbose) {
            cout << "  Step " << (t+1) << ": " << htn->taskNames[selectedAction] 
                 << " | |A_" << (t+1) << "| = " << applicableCount 
                 << " | P = " << scientific << stepProb << endl;
        }
        
        // Execute the action
        applyAction(htn, currentState, selectedAction);
        remaining.erase(selectedAction);
        executed.insert(selectedAction);
    }
    
    double stage2_prob = exp(logProb);
    
    if (verbose) {
        cout << "\nlog P(π | N, s_0) = " << scientific << logProb << " nats" << endl;
        cout << "P(π | N, s_0) = " << scientific << stage2_prob << endl;
    }
    
    return stage2_prob;
}

// ============================================================================
// STAGE III: OBSERVATION GENERATION PROBABILITY P(ô | π)
// ============================================================================

double progressPrior(int t, int planLength) {
    return 1.0 / (planLength + 1);
}

double alignmentLikelihoodFullObs(const vector<int>& observations, const vector<int>& planPrefix) {
    if (observations.size() != planPrefix.size()) {
        return 0.0;
    }
    
    for (size_t i = 0; i < observations.size(); i++) {
        if (observations[i] != planPrefix[i]) {
            return 0.0;
        }
    }
    
    return 1.0;
}

double alignmentLikelihoodPartialObs(const vector<int>& observations, 
                                     const vector<int>& planPrefix, 
                                     double pDet) {
    int m = observations.size();
    int n = planPrefix.size();
    
    if (m > n) return 0.0;
    
    // DP table: dp[i][j] = P(ô_{1:i} | π_{1:j})
    vector<vector<double>> dp(m + 1, vector<double>(n + 1, 0.0));
    dp[0][0] = 1.0;
    
    for (int j = 1; j <= n; j++) {
        dp[0][j] = dp[0][j-1] * (1 - pDet);
    }
    
    for (int i = 1; i <= m; i++) {
        for (int j = i; j <= n; j++) {
            double match = 0.0;
            if (observations[i-1] == planPrefix[j-1]) {
                match = dp[i-1][j-1] * pDet;
            }
            double skip = dp[i][j-1] * (1 - pDet);
            dp[i][j] = match + skip;
        }
    }
    
    return dp[m][n];
}

double computeStage3Probability(const vector<int>& observations, 
                               const vector<int>& plan,
                               bool fullObservability,
                               double pDet,
                               Model* htn,
                               bool verbose = true) {
    
    if (verbose) {
        cout << "\n=== STAGE III: Observation Generation ===" << endl;
        cout << "Observations: " << observations.size() << " actions" << endl;
        cout << "Plan: " << plan.size() << " actions" << endl;
        cout << "Full observability: " << (fullObservability ? "yes" : "no") << endl;
    }
    
    if (fullObservability) {
        double progress = progressPrior(observations.size(), plan.size());
        vector<int> planPrefix(plan.begin(), plan.begin() + min(observations.size(), plan.size()));
        double alignment = alignmentLikelihoodFullObs(observations, planPrefix);
        double prob = progress * alignment;
        
        if (verbose) {
            cout << "P(Execute " << observations.size() << " actions | π) = " << progress << endl;
            cout << "1[π_{1:" << observations.size() << "} = ô] = " << alignment << endl;
            cout << "P(ô | π) = " << prob << endl;
        }
        
        return prob;
    } else {
        double totalProb = 0.0;
        
        if (verbose) {
            cout << "\nMarginalizing over execution progress:" << endl;
        }
        
        for (size_t t = observations.size(); t <= plan.size(); t++) {
            double progress = progressPrior(t, plan.size());
            vector<int> planPrefix(plan.begin(), plan.begin() + t);
            double alignment = alignmentLikelihoodPartialObs(observations, planPrefix, pDet);
            double contribution = progress * alignment;
            totalProb += contribution;
            
            if (verbose && contribution > 1e-10) {
                cout << "  t=" << t << ": P(Execute " << t << " | π) = " << progress 
                     << ", P(ô | π_{1:" << t << "}) = " << alignment 
                     << ", contribution = " << contribution << endl;
            }
        }
        
        if (verbose) {
            cout << "\nP(ô | π) = " << totalProb << endl;
        }
        
        return totalProb;
    }
}

// ============================================================================
// MAIN: NORMALIZED LIKELIHOOD COMPUTATION
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 4) {
        cout << "Usage: " << argv[0] 
             << " <model.psas> <observation_plan_log> <baseline_plan_log> [alpha=1.0] [num_obs=all] [full_obs=1] [p_det=0.9]" << endl;
        cout << "\nArguments:" << endl;
        cout << "  model.psas            : Grounded HTN model" << endl;
        cout << "  observation_plan_log  : Log file with plan embedding observations (π^+)" << endl;
        cout << "  baseline_plan_log     : Log file with unconstrained baseline plan (π_base)" << endl;
        cout << "  alpha                 : Inverse temperature for Stage I (default: 1.0)" << endl;
        cout << "  num_obs               : Number of observations to use (default: all)" << endl;
        cout << "  full_obs              : 1 for full observability, 0 for partial (default: 1)" << endl;
        cout << "  p_det                 : Detection probability for partial obs (default: 0.9)" << endl;
        cout << "\nComputes normalized likelihood:" << endl;
        cout << "  P̂(ô | N^g, s_0) ≈ P̃(ô, π^+, N^+ | N^g, s_0) / P̃(N_base, π_base | N^g, s_0)" << endl;
        return 1;
    }
    
    string modelFile = argv[1];
    string observationLogFile = argv[2];
    string baselineLogFile = argv[3];
    double alpha = (argc > 4) ? atof(argv[4]) : 1.0;
    int numObservations = (argc > 5) ? atoi(argv[5]) : -1;
    bool fullObservability = (argc > 6) ? (atoi(argv[6]) != 0) : true;
    double pDet = (argc > 7) ? atof(argv[7]) : 0.9;
    
    cout << "============================================================" << endl;
    cout << "Normalized HTN Goal Recognition Likelihood" << endl;
    cout << "============================================================" << endl;
    cout << "\nInput:" << endl;
    cout << "  Model: " << modelFile << endl;
    cout << "  Observation plan log: " << observationLogFile << endl;
    cout << "  Baseline plan log: " << baselineLogFile << endl;
    cout << "  α (Stage I): " << alpha << endl;
    cout << "  Observability: " << (fullObservability ? "Full" : "Partial") << endl;
    if (!fullObservability) {
        cout << "  p_det: " << pDet << endl;
    }
    
    // Load HTN model
    Model* htn = new Model();
    htn->read(modelFile);
    
    // Parse observation plan (π^+)
    vector<string> obsPlanStrings = parsePlanFromLog(observationLogFile);
    if (obsPlanStrings.empty()) {
        cerr << "Error: No plan found in observation log file" << endl;
        return 1;
    }
    
    vector<int> obsPlan;
    for (const string& actionStr : obsPlanStrings) {
        int actionId = findTaskId(htn, actionStr);
        if (actionId >= 0 && actionId < htn->numActions) {
            obsPlan.push_back(actionId);
        }
    }
    
    // Parse baseline plan (π_base)
    vector<string> basePlanStrings = parsePlanFromLog(baselineLogFile);
    if (basePlanStrings.empty()) {
        cerr << "Error: No plan found in baseline log file" << endl;
        return 1;
    }
    
    vector<int> basePlan;
    for (const string& actionStr : basePlanStrings) {
        int actionId = findTaskId(htn, actionStr);
        if (actionId >= 0 && actionId < htn->numActions) {
            basePlan.push_back(actionId);
        }
    }
    
    cout << "\nObservation plan (π^+): " << obsPlan.size() << " actions" << endl;
    cout << "Baseline plan (π_base): " << basePlan.size() << " actions" << endl;
    
    // Prepare observations
    vector<int> observations;
    if (numObservations < 0 || numObservations > (int)obsPlan.size()) {
        observations = obsPlan;
        numObservations = obsPlan.size();
    } else {
        observations = vector<int>(obsPlan.begin(), obsPlan.begin() + numObservations);
    }
    
    cout << "Using " << numObservations << " observations" << endl;
    
    // Parse decomposition trees to get task-method counts and track used methods
    set<int> usedMethodIds;
    // bool obsTopWrapperSeen = false;
    map<string, int> obsTaskMethodCounts = parseDecompositionTreeFromLog(observationLogFile, htn, &usedMethodIds);
    map<string, int> baseTaskMethodCounts = parseDecompositionTreeFromLog(baselineLogFile, htn, &usedMethodIds);

    // if (obsTopWrapperSeen) {
    //     map<string, int> filteredBaseCounts;
    //     auto topIt = baseTaskMethodCounts.find("__top[]");
    //     if (topIt != baseTaskMethodCounts.end()) {
    //         filteredBaseCounts["__top[]"] = topIt->second;
    //     }
    //     baseTaskMethodCounts = filteredBaseCounts;
    // }
    
    // Extract ordering constraints only from methods actually used in the decomposition
    cout << "Extracting ordering constraints from " << usedMethodIds.size() << " used methods (out of " << htn->numMethods << " total)..." << endl;
    set<pair<int,int>> orderingConstraints = extractOrderingConstraints(htn, &usedMethodIds);
    cout << "Extraction complete. Found " << orderingConstraints.size() << " ordering constraints." << endl;
    
    // ========================================================================
    // STEP 1: COMPUTE NUMERATOR P̃(ô, π^+, N^+ | N^g, s_0)
    // ========================================================================
    
    cout << "\n" << string(60, '=') << endl;
    cout << "STEP 1: Numerator - Observation-Consistent Execution" << endl;
    cout << string(60, '=') << endl;
    double obs_stage1 = computeStage1Probability(obsTaskMethodCounts, true);
    double obs_stage2 = computeStage2Probability(htn, obsPlan, orderingConstraints, true);
    double obs_stage3 = computeStage3Probability(observations, obsPlan, fullObservability, pDet, htn, true);
    
    double numerator = obs_stage1 * obs_stage2 * obs_stage3;
    
    cout << "\nNumerator: P̃(ô, π^+, N^+ | N^g, s_0) = " << scientific << numerator << endl;
    
    // ========================================================================
    // STEP 2: COMPUTE DENOMINATOR P̃(N_base, π_base | N^g, s_0)
    // ========================================================================
    
    cout << "\n" << string(60, '=') << endl;
    cout << "STEP 2: Denominator - Baseline Unconstrained Execution" << endl;
    cout << string(60, '=') << endl;
    
    // Use pre-parsed baseline task-method counts
    double base_stage1 = computeStage1Probability(baseTaskMethodCounts, true);
    double base_stage2 = computeStage2Probability(htn, basePlan, orderingConstraints, true);
    
    double denominator = base_stage1 * base_stage2;
    
    cout << "\nDenominator: P̃(N_base, π_base | N^g, s_0) = " << scientific << denominator << endl;
    
    // ========================================================================
    // STEP 3: COMPUTE NORMALIZED LIKELIHOOD
    // ========================================================================
    
    double normalized_likelihood = numerator / denominator;
    
    cout << "\n" << string(60, '=') << endl;
    cout << "FINAL RESULTS" << endl;
    cout << string(60, '=') << endl;
    cout << fixed << setprecision(10);
    cout << "\nNumerator (ô, π^+, N^+):" << endl;
    cout << "  Stage I:   P(N^+ | N^g)       = " << obs_stage1 << endl;
    cout << "  Stage II:  P(π^+ | N^+, s_0)  = " << obs_stage2 << endl;
    cout << "  Stage III: P(ô | π^+)         = " << obs_stage3 << endl;
    cout << "  Product:   P̃(ô, π^+, N^+)    = " << scientific << numerator << endl;
    
    cout << "\nDenominator (baseline):" << endl;
    cout << "  Stage I:   P(N_base | N^g)          = " << fixed << base_stage1 << endl;
    cout << "  Stage II:  P(π_base | N_base, s_0)  = " << scientific << base_stage2 << endl;
    cout << "  Product:   P̃(N_base, π_base)       = " << denominator << endl;
    
    cout << "\n" << string(60, '-') << endl;
    cout << "Normalized Likelihood:" << endl;
    cout << "  P̂(ô | N^g, s_0) = " << normalized_likelihood << endl;
    cout << "  log P̂(ô | N^g, s_0) = " << fixed << log(normalized_likelihood) << endl;
    cout << string(60, '=') << endl;
    
    delete htn;
    return 0;
}


