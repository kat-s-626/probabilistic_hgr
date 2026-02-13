/**
 * Posterior Estimation Helper Program
 * 
 * Provides utilities for iterative posterior estimation:
 * 1. Extract hypothesis selections from planner logs
 * 2. Manipulate HDDL domain files (remove hypotheses)
 * 3. Create problem files with specific goals
 * 4. Compute normalized posterior probabilities
 * 
 * Usage:
 *   ./posterior_helper extract <log_file>
 *   ./posterior_helper remove <domain_file> <hypothesis> <output_file>
 *   ./posterior_helper problem <template> <goal_task> <output_file>
 *   ./posterior_helper normalize <hyp1:lik1> <hyp2:lik2> ...
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <iomanip>

using namespace std;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    size_t end = str.find_last_not_of(" \t\r\n");
    
    if (start == string::npos) return "";
    return str.substr(start, end - start + 1);
}

vector<string> split(const string& str, char delimiter) {
    vector<string> tokens;
    stringstream ss(str);
    string token;
    
    while (getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

bool startsWith(const string& str, const string& prefix) {
    if (str.length() < prefix.length()) return false;
    return str.substr(0, prefix.length()) == prefix;
}

bool contains(const string& str, const string& substr) {
    return str.find(substr) != string::npos;
}

// ============================================================================
// HYPOTHESIS EXTRACTION
// ============================================================================

// Extract instantiated subtasks from log file
// Returns a string like "(and (makeNoodles spaghetti pot1) (makeBolognese pan1))"
string extractInstantiatedSubtasks(const string& logFile) {
    ifstream file(logFile);
    if (!file.is_open()) {
        cerr << "Error: Cannot open log file: " << logFile << endl;
        return "";
    }
    
    // Two-pass approach: first find hypothesis name, then find splitted lines
    string hypothesisName = "";
    vector<string> allLines;
    string line;
    bool inDecompTree = false;
    
    // Pass 1: Read all lines and find hypothesis name
    while (getline(file, line)) {
        line = trim(line);
        
        if (contains(line, "root 0")) {
            inDecompTree = true;
        }
        
        if (inDecompTree) {
            allLines.push_back(line);
            
            // Find hypothesis name from mtlt decomposition
            if (hypothesisName.empty() && (contains(line, "mtlt[]") || contains(line, "tlt[]") || contains(line, "__top[] ->"))) {
                size_t arrowPos = line.find("->");
                if (arrowPos != string::npos) {
                    string afterArrow = line.substr(arrowPos + 2);
                    afterArrow = trim(afterArrow);
                    
                    // Extract hypothesis name (first token)
                    vector<string> parts = split(afterArrow, ' ');

                    // print debug info
                    cout << "Debug: Found candidate hypothesis line: " << line << endl;
                    if (parts.size() > 0) {
                        hypothesisName = trim(parts[0]);
                    }
                }
            }
        }
    }
    
    if (hypothesisName.empty()) {
        return "";
    }
    
    // Pass 2: Find splitted lines with actual subtasks
    vector<string> tasks;
    for (const string& l : allLines) {
        // Look for hypothesis_splitted lines
        // Pattern: "1089 hypothesis-1_splitted_1088[] -> <...;makeBolognese[pan1];...>"
        if (contains(l, hypothesisName) && contains(l, "_splitted")) {
            size_t arrowPos = l.find("->");
            if (arrowPos == string::npos) continue;
            
            string afterArrow = l.substr(arrowPos + 2);
            afterArrow = trim(afterArrow);
            
            // Look for method encoding with subtasks
            // Pattern: "<...; subtask[params];...>"
            if (startsWith(afterArrow, "<")) {
                size_t start = 1;  // Skip <
                size_t end = afterArrow.find('>');
                if (end == string::npos) continue;
                
                string methodEncoding = afterArrow.substr(start, end - start);
                vector<string> parts = split(methodEncoding, ';');
                
                // Look for task[params] patterns (skip method names starting with m-, numbers, etc.)
                for (const string& part : parts) {
                    string task = trim(part);
                    if (task.empty() || startsWith(task, "m-") || startsWith(task, "0") || 
                        startsWith(task, "-") || task.find_first_not_of("0123456789,-") == string::npos ||
                        startsWith(task, "_")) {
                        continue;  // Skip method names and numeric encodings
                    }
                    
                    // Convert task[param1,param2] to (task param1 param2)
                    size_t bracketPos = task.find('[');
                    if (bracketPos != string::npos) {
                        string taskName = task.substr(0, bracketPos);
                        size_t closeBracket = task.find(']');
                        if (closeBracket != string::npos) {
                            string params = task.substr(bracketPos + 1, closeBracket - bracketPos - 1);
                            // Replace commas with spaces
                            for (char& c : params) {
                                if (c == ',') c = ' ';
                            }
                            tasks.push_back("(" + taskName + " " + params + ")");
                        }
                    }
                }
            }
        }
    }
    
    // Build result
    if (tasks.size() == 0) {
        return "";
    } else if (tasks.size() == 1) {
        return tasks[0];
    } else {
        string result = "(and";
        for (const string& t : tasks) {
            result += " " + t;
        }
        result += ")";
        return result;
    }
}

string extractHypothesisFromLog(const string& logFile) {
    ifstream file(logFile);
    if (!file.is_open()) {
        cerr << "Error: Cannot open log file: " << logFile << endl;
        return "";
    }
    
    string line;
    bool inDecompTree = false;
    
    while (getline(file, line)) {
        line = trim(line);
        
        // Strategy 1: Look for mtlt/tlt decomposition in decomposition tree
        // Pattern: "37 mtlt[] -> hypothesis-1 ..." or "436 mtlt[] -> <<hypothesis-29;..."
        if (inDecompTree && (contains(line, "mtlt[]") || contains(line, "tlt[]"))) {
            size_t arrowPos = line.find("->");
            if (arrowPos != string::npos) {
                // Extract hypothesis name after the arrow
                string afterArrow = line.substr(arrowPos + 2);
                afterArrow = trim(afterArrow);
                
                // Remove method encoding if present (e.g., "<<hypothesis-29;..." -> "hypothesis-29")
                if (startsWith(afterArrow, "<<")) {
                    // Extract content between << and first ;
                    size_t start = 2;  // Skip <<
                    size_t end = afterArrow.find(';');
                    if (end != string::npos) {
                        afterArrow = afterArrow.substr(start, end - start);
                        afterArrow = trim(afterArrow);
                    }
                } else if (startsWith(afterArrow, "<")) {
                    // Single < method encoding
                    size_t start = 1;
                    size_t end = afterArrow.find(';');
                    if (end != string::npos) {
                        afterArrow = afterArrow.substr(start, end - start);
                        afterArrow = trim(afterArrow);
                    }
                }
                
                // Get first token (hypothesis name)
                vector<string> parts = split(afterArrow, ' ');
                if (parts.size() > 0) {
                    string hypothesis = trim(parts[0]);
                    if (!hypothesis.empty() && !startsWith(hypothesis, "__")) {
                        return hypothesis;
                    }
                }
            }
        }
        
        // Strategy 2: Look for abstract task decomposition in plan
        // Pattern: "0 <abs> hypothesis_name -> method_name"
        if (contains(line, "<abs>") && contains(line, "->")) {
            size_t absPos = line.find("<abs>");
            size_t arrowPos = line.find("->");
            
            if (absPos != string::npos && arrowPos != string::npos) {
                string between = line.substr(absPos + 5, arrowPos - absPos - 5);
                between = trim(between);
                
                // Filter out system tasks and prefix actions
                if (!startsWith(between, "__") && 
                    !startsWith(between, "_!") &&
                    !contains(between, "[") &&
                    !between.empty()) {
                    return between;
                }
            }
        }
        
        // Detect decomposition tree section
        if (startsWith(line, "root ")) {
            inDecompTree = true;
            continue;
        }
        
        // End of decomposition tree
        if (startsWith(line, "<==") || startsWith(line, "===")) {
            inDecompTree = false;
        }
    }
    
    file.close();
    return "";
}

// ============================================================================
// DOMAIN MANIPULATION
// ============================================================================

bool removeHypothesisFromDomain(const string& domainFile, 
                                const string& hypothesis,
                                const string& outputFile) {
    ifstream inFile(domainFile);
    if (!inFile.is_open()) {
        cerr << "Error: Cannot open domain file: " << domainFile << endl;
        return false;
    }
    
    vector<string> lines;
    string line;
    
    // Read all lines
    while (getline(inFile, line)) {
        lines.push_back(line);
    }
    inFile.close();
    
    // Process lines and comment out hypothesis-related blocks
    vector<string> outputLines;
    bool commentBlock = false;
    int parenCount = 0;
    int blockStartParen = 0;
    
    for (size_t i = 0; i < lines.size(); i++) {
        line = lines[i];
        string trimmedLine = trim(line);
        
        if (!commentBlock) {
            // Check if this line starts a method definition for the hypothesis
            // Pattern: "  (:method hypothesis-1" or "  (:method hypothesis-10"
            if (contains(line, ":method") && contains(line, hypothesis)) {
                // Verify this is the exact hypothesis (not hypothesis-1 matching hypothesis-10)
                string afterMethod = line.substr(line.find(":method") + 7);
                afterMethod = trim(afterMethod);
                
                // Check if hypothesis name matches (with word boundary)
                if (startsWith(afterMethod, hypothesis) && 
                    (afterMethod.length() == hypothesis.length() || 
                     !isalnum(afterMethod[hypothesis.length()]))) {
                    commentBlock = true;
                    blockStartParen = 0;
                    parenCount = 0;
                    
                    // Count parentheses on this line
                    for (char c : line) {
                        if (c == '(') {
                            parenCount++;
                            if (blockStartParen == 0) blockStartParen = parenCount;
                        }
                        if (c == ')') parenCount--;
                    }
                    
                    // Comment out this line
                    outputLines.push_back(";; REMOVED: " + line);
                    continue;
                }
            }
            
            // If not commenting, add line as-is
            outputLines.push_back(line);
        } else {
            // We are in a block to comment out
            // Update paren count
            for (char c : line) {
                if (c == '(') parenCount++;
                if (c == ')') parenCount--;
            }
            
            // Comment out this line
            outputLines.push_back(";; REMOVED: " + line);
            
            // End of block when we return to the starting parenthesis level
            if (parenCount < blockStartParen) {
                commentBlock = false;
            }
        }
    }
    
    // Write output file
    ofstream outFile(outputFile);
    if (!outFile.is_open()) {
        cerr << "Error: Cannot write to output file: " << outputFile << endl;
        return false;
    }
    
    for (const string& outLine : outputLines) {
        outFile << outLine << endl;
    }
    outFile.close();
    
    return true;
}

// ============================================================================
// PROBLEM FILE CREATION
// ============================================================================

string extractSubtasksFromMethod(const string& domainFile, const string& methodName) {
    ifstream inFile(domainFile);
    if (!inFile.is_open()) {
        cerr << "Error: Cannot open domain file: " << domainFile << endl;
        return "";
    }
    
    vector<string> lines;
    string line;
    while (getline(inFile, line)) {
        lines.push_back(line);
    }
    inFile.close();
    
    // Find the method definition
    bool inMethod = false;
    bool inSubtasks = false;
    string subtasksContent = "";
    int parenCount = 0;
    
    for (size_t i = 0; i < lines.size(); i++) {
        line = lines[i];
        
        // Check if this line starts the method we're looking for
        if (!inMethod && contains(line, ":method") && contains(line, methodName)) {
            inMethod = true;
            continue;
        }
        
        if (inMethod) {
            // Look for :subtasks section
            if (contains(line, ":subtasks")) {
                inSubtasks = true;
                // Start capturing from this line
                size_t subtasksPos = line.find(":subtasks");
                if (subtasksPos != string::npos) {
                    string afterSubtasks = line.substr(subtasksPos + 9);
                    subtasksContent += afterSubtasks;
                    
                    // Count parentheses
                    for (char c : afterSubtasks) {
                        if (c == '(') parenCount++;
                        if (c == ')') parenCount--;
                    }
                }
                continue;
            }
            
            // If we're in subtasks section, keep capturing
            if (inSubtasks) {
                subtasksContent += " " + line;
                for (char c : line) {
                    if (c == '(') parenCount++;
                    if (c == ')') parenCount--;
                }
                
                // Check if we've closed the subtasks section
                if (parenCount == 0) {
                    break;
                }
            }
            
            // Check if we've ended the method
            if (trim(line) == ")" && !inSubtasks) {
                break;
            }
        }
    }
    
    return trim(subtasksContent);
}

bool createProblemWithGoal(const string& templateFile,
                          const string& goalTask,
                          const string& outputFile) {
    ifstream inFile(templateFile);
    if (!inFile.is_open()) {
        cerr << "Error: Cannot open template file: " << templateFile << endl;
        return false;
    }
    
    vector<string> lines;
    string line;
    while (getline(inFile, line)) {
        lines.push_back(line);
    }
    inFile.close();
    
    // Process line by line to comment out old tasks and add new one
    vector<string> newLines;
    bool inHtnSection = false;
    bool foundTasks = false;
    bool inTasksSection = false;
    int tasksSectionDepth = 0;
    
    for (size_t i = 0; i < lines.size(); i++) {
        line = lines[i];
        string trimmedLine = trim(line);
        
        // Check if we're in HTN section
        if (line.find("(:htn") != string::npos) {
            inHtnSection = true;
            // If this line doesn't contain :tasks, keep it as-is.
            if (line.find(":tasks") == string::npos) {
                newLines.push_back(line);
                continue;
            }
        }
        
        // Check if we found an active :tasks line (not commented)
        if (inHtnSection && !foundTasks && line.find(":tasks") != string::npos) {
            // Check if this line is commented
            size_t firstNonSpace = line.find_first_not_of(" \t");
            bool isCommented = (firstNonSpace != string::npos && line[firstNonSpace] == ';');
            
            if (!isCommented) {
                // This is the active tasks line - we'll replace it
                foundTasks = true;
                inTasksSection = true;
                
                // Get indentation from original line
                size_t indent = 0;
                while (indent < line.length() && (line[indent] == ' ' || line[indent] == '\t')) {
                    indent++;
                }
                string indentStr = line.substr(0, indent);
                
                // Add new tasks line. If :htn and :tasks are on the same line,
                // preserve the (:htn prefix to keep the HDDL structure valid.
                if (line.find("(:htn") != string::npos) {
                    newLines.push_back(indentStr + "(:htn :tasks " + goalTask + ")");
                } else {
                    newLines.push_back(indentStr + ":tasks " + goalTask);
                }
                
                // Comment out the old line
                newLines.push_back(";" + line);
                
                // Count parentheses to track when tasks section ends
                for (char c : line) {
                    if (c == '(') tasksSectionDepth++;
                    if (c == ')') tasksSectionDepth--;
                }
                
                // If tasks section is complete on one line, we're done
                if (tasksSectionDepth == 0) {
                    inTasksSection = false;
                }
                continue;
            }
        }
        
        // If we're in the tasks section we're replacing, comment out these lines too
        if (inTasksSection) {
            // Count parentheses
            for (char c : line) {
                if (c == '(') tasksSectionDepth++;
                if (c == ')') tasksSectionDepth--;
            }
            
            // Comment out this line
            newLines.push_back(";" + line);
            
            // Check if we've finished the tasks section
            if (tasksSectionDepth == 0) {
                inTasksSection = false;
            }
            continue;
        }
        
        // Check if we're leaving HTN section
        if (inHtnSection && trimmedLine.find(":ordering") != string::npos) {
            inHtnSection = false;
        }
        
        // Default: keep line as-is
        newLines.push_back(line);
    }
    
    // Write output file
    ofstream outFile(outputFile);
    if (!outFile.is_open()) {
        cerr << "Error: Cannot write to output file: " << outputFile << endl;
        return false;
    }
    
    for (const string& outLine : newLines) {
        outFile << outLine << "\n";
    }
    outFile.close();
    
    return true;
}

// ============================================================================
// GENERATE MTLT VERSION
// ============================================================================

/**
 * Generate HDDL problem file with tasks commented out and replaced with mtlt/tlt.
 * This creates a version suitable for goal recognition where the specific tasks
 * are hidden and replaced with a generic top-level task placeholder.
 * 
 * Automatically parses the :tasks section to determine task count and generate
 * appropriate placeholder (mtlt for multiple tasks, tlt for single task).
 * 
 * @param hddlFile Input HDDL problem file
 * @param outputFile Output HDDL problem file
 * @return Task placeholder used ("mtlt" for multiple tasks, "tlt" for single task)
 */
string generateMtltVersion(const string& hddlFile, 
                          const string& outputFile) {
    ifstream inFile(hddlFile);
    if (!inFile.is_open()) {
        cerr << "Error: Cannot open input file: " << hddlFile << endl;
        return "";
    }
    
    vector<string> lines;
    string line;
    
    // Read all lines
    while (getline(inFile, line)) {
        lines.push_back(line);
    }
    inFile.close();
    
    // First pass: count tasks to determine mtlt vs tlt
    int taskCount = 0;
    bool inHtnSection = false;
    bool inTasksSection = false;
    int parenDepth = 0;
    bool countingTasks = false;
    
    for (size_t i = 0; i < lines.size(); i++) {
        line = lines[i];
        string trimmedLine = trim(line);
        
        if (line.find(":htn") != string::npos) {
            inHtnSection = true;
        }
        
        if (inHtnSection && line.find(":tasks") != string::npos && !inTasksSection) {
            inTasksSection = true;
            countingTasks = true;
            
            // Start counting parentheses after :tasks
            size_t tasksPos = line.find(":tasks");
            string afterTasks = line.substr(tasksPos + 6);
            
            for (char c : afterTasks) {
                if (c == '(') parenDepth++;
                else if (c == ')') parenDepth--;
            }
            
            // If line has task content, check for (and ...)
            if (afterTasks.find("(and") != string::npos) {
                // Multiple tasks with (and ...)
                countingTasks = true;
            } else if (parenDepth == 0) {
                // Single task on same line
                taskCount = 1;
                inTasksSection = false;
            }
            continue;
        }
        
        if (countingTasks && inTasksSection) {
            // Count tasks by looking for opening parentheses at depth 1
            for (char c : line) {
                if (c == '(') {
                    parenDepth++;
                    // Task starts at depth 2 when inside (and ...)
                    if (parenDepth == 2) taskCount++;
                }
                else if (c == ')') parenDepth--;
            }
            
            if (parenDepth == 0) {
                inTasksSection = false;
                countingTasks = false;
            }
            
            if (trimmedLine.find(":ordering") != string::npos) {
                inTasksSection = false;
                countingTasks = false;
                break;
            }
        }
    }
    
    // Determine placeholder: mtlt (multiple tasks) or tlt (single task)
    string taskPlaceholder = (taskCount > 1) ? "mtlt" : "tlt";
    
    // Second pass: generate output with tasks commented out
    vector<string> newLines;
    inHtnSection = false;
    inTasksSection = false;
    bool tasksFound = false;
    
    for (size_t i = 0; i < lines.size(); i++) {
        line = lines[i];
        string trimmedLine = trim(line);
        
        // Check if we're entering HTN section
        if (line.find(":htn") != string::npos) {
            inHtnSection = true;
            newLines.push_back(line);
            continue;
        }
        
        // Check if we found tasks section
        if (inHtnSection && line.find(":tasks") != string::npos && !tasksFound) {
            // Skip if this is already a commented line
            if (trimmedLine[0] == ';') {
                newLines.push_back(line);
                continue;
            }
            
            inTasksSection = true;
            tasksFound = true;
            
            // Get indentation from current line
            size_t indent = 0;
            while (indent < line.length() && (line[indent] == ' ' || line[indent] == '\t')) {
                indent++;
            }
            string indentStr = line.substr(0, indent);
            
            // Add the placeholder task
            newLines.push_back(indentStr + ":tasks (" + taskPlaceholder + ")");
            
            // Comment out the original line
            newLines.push_back(";" + line);
            continue;
        }
        
        // If we're in the tasks section, comment out task lines
        if (inTasksSection) {
            // Check if we've reached :ordering or :constraints
            if (trimmedLine.find(":ordering") != string::npos || 
                trimmedLine.find(":constraints") != string::npos) {
                inTasksSection = false;
                newLines.push_back(line);
                continue;
            }
            
            // Comment out task content lines
            newLines.push_back(";" + line);
            continue;
        }
        
        // Default: add line as-is
        newLines.push_back(line);
    }
    
    // Write output file
    ofstream outFile(outputFile);
    if (!outFile.is_open()) {
        cerr << "Error: Cannot write to output file: " << outputFile << endl;
        return "";
    }
    
    for (const string& outLine : newLines) {
        outFile << outLine << "\n";
    }
    outFile.close();
    
    return taskPlaceholder;
}

// ============================================================================
// POSTERIOR NORMALIZATION
// ============================================================================

vector<pair<string, double>> computeNormalizedPosteriors(
    const vector<pair<string, double>>& likelihoods) {
    
    // Compute sum of likelihoods
    double total = 0.0;
    for (const auto& pair : likelihoods) {
        total += pair.second;
    }
    
    vector<pair<string, double>> posteriors;
    
    if (total == 0.0) {
        // All likelihoods zero, return uniform distribution
        double uniform = 1.0 / likelihoods.size();
        for (const auto& pair : likelihoods) {
            posteriors.push_back({pair.first, uniform});
        }
    } else {
        // Normalize by total
        for (const auto& pair : likelihoods) {
            posteriors.push_back({pair.first, pair.second / total});
        }
    }
    
    return posteriors;
}

// ============================================================================
// MAIN
// ============================================================================

void printUsage(const char* progName) {
    cout << "Posterior Estimation Helper Program" << endl;
    cout << "====================================" << endl;
    cout << endl;
    cout << "Usage:" << endl;
    cout << "  " << progName << " extract <log_file>" << endl;
    cout << "      Extract hypothesis from planner log file" << endl;
    cout << endl;
    cout << "  " << progName << " instantiated <log_file>" << endl;
    cout << "      Extract instantiated subtasks from planner log file" << endl;
    cout << endl;
    cout << "  " << progName << " subtasks <domain_file> <hypothesis_method>" << endl;
    cout << "      Extract subtasks from hypothesis method definition" << endl;
    cout << endl;
    cout << "  " << progName << " remove <domain_file> <hypothesis> <output_file>" << endl;
    cout << "      Remove hypothesis from domain file" << endl;
    cout << endl;
    cout << "  " << progName << " problem <template> <goal_task> <output_file>" << endl;
    cout << "      Create problem file with specified goal task" << endl;
    cout << endl;
    cout << "  " << progName << " mtlt <hddl_file> <output_file>" << endl;
    cout << "      Generate mtlt/tlt version with tasks commented out" << endl;
    cout << endl;
    cout << "  " << progName << " normalize <hyp1:lik1> <hyp2:lik2> ..." << endl;
    cout << "      Compute normalized posterior probabilities" << endl;
    cout << endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    string command = argv[1];
    
    // ========================================================================
    // EXTRACT HYPOTHESIS
    // ========================================================================
    
    if (command == "extract") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " extract <log_file>" << endl;
            return 1;
        }
        
        string logFile = argv[2];
        string hypothesis = extractHypothesisFromLog(logFile);
        
        if (!hypothesis.empty()) {
            cout << hypothesis << endl;
            return 0;
        } else {
            cerr << "Error: Could not extract hypothesis from log" << endl;
            return 1;
        }
    }
    
    // ========================================================================
    // EXTRACT INSTANTIATED SUBTASKS FROM LOG
    // ========================================================================
    
    else if (command == "instantiated") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " instantiated <log_file>" << endl;
            return 1;
        }
        
        string logFile = argv[2];
        string subtasks = extractInstantiatedSubtasks(logFile);
        
        if (!subtasks.empty()) {
            cout << subtasks << endl;
            return 0;
        } else {
            cerr << "Error: Could not extract instantiated subtasks from log" << endl;
            return 1;
        }
    }
    
    // ========================================================================
    // EXTRACT SUBTASKS FROM HYPOTHESIS METHOD
    // ========================================================================
    
    else if (command == "subtasks") {
        if (argc < 4) {
            cerr << "Usage: " << argv[0] << " subtasks <domain_file> <hypothesis_method>" << endl;
            return 1;
        }
        
        string domainFile = argv[2];
        string methodName = argv[3];
        string subtasks = extractSubtasksFromMethod(domainFile, methodName);
        
        if (!subtasks.empty()) {
            cout << subtasks << endl;
            return 0;
        } else {
            cerr << "Error: Could not extract subtasks from method" << endl;
            return 1;
        }
    }
    
    // ========================================================================
    // REMOVE HYPOTHESIS FROM DOMAIN
    // ========================================================================
    
    else if (command == "remove") {
        if (argc < 5) {
            cerr << "Usage: " << argv[0] << " remove <domain_file> <hypothesis> <output_file>" << endl;
            return 1;
        }
        
        string domainFile = argv[2];
        string hypothesis = argv[3];
        string outputFile = argv[4];
        
        bool success = removeHypothesisFromDomain(domainFile, hypothesis, outputFile);
        
        if (success) {
            cout << "Removed " << hypothesis << " from domain" << endl;
            cout << "Output written to: " << outputFile << endl;
            return 0;
        } else {
            cerr << "Error: Failed to remove hypothesis" << endl;
            return 1;
        }
    }
    
    // ========================================================================
    // CREATE PROBLEM WITH GOAL
    // ========================================================================
    
    else if (command == "problem") {
        if (argc < 5) {
            cerr << "Usage: " << argv[0] << " problem <template> <goal_task> <output_file>" << endl;
            return 1;
        }
        
        string templateFile = argv[2];
        string goalTask = argv[3];
        string outputFile = argv[4];
        
        bool success = createProblemWithGoal(templateFile, goalTask, outputFile);
        
        if (success) {
            cout << "Created problem with goal: " << goalTask << endl;
            cout << "Output written to: " << outputFile << endl;
            return 0;
        } else {
            cerr << "Error: Failed to create problem" << endl;
            return 1;
        }
    }
    
    // ========================================================================
    // GENERATE MTLT VERSION
    // ========================================================================
    
    else if (command == "mtlt") {
        if (argc < 4) {
            cerr << "Usage: " << argv[0] << " mtlt <hddl_file> <output_file>" << endl;
            return 1;
        }
        
        string hddlFile = argv[2];
        string outputFile = argv[3];
        
        string placeholder = generateMtltVersion(hddlFile, outputFile);
        
        if (!placeholder.empty()) {
            cout << "Generated " << placeholder << " version" << endl;
            cout << "Output written to: " << outputFile << endl;
            return 0;
        } else {
            cerr << "Error: Failed to generate mtlt version" << endl;
            return 1;
        }
    }
    
    // ========================================================================
    // NORMALIZE POSTERIORS
    // ========================================================================
    
    else if (command == "normalize") {
        if (argc < 3) {
            cerr << "Usage: " << argv[0] << " normalize <hyp1:lik1> <hyp2:lik2> ..." << endl;
            return 1;
        }
        
        vector<pair<string, double>> likelihoods;
        
        for (int i = 2; i < argc; i++) {
            string arg = argv[i];
            size_t colonPos = arg.find(':');
            
            if (colonPos == string::npos) {
                cerr << "Error: Invalid format (expected hyp:lik): " << arg << endl;
                return 1;
            }
            
            string hyp = arg.substr(0, colonPos);
            string likStr = arg.substr(colonPos + 1);
            
            try {
                double lik = stod(likStr);
                likelihoods.push_back({hyp, lik});
            } catch (const exception& e) {
                cerr << "Error: Invalid likelihood value: " << likStr << endl;
                return 1;
            }
        }
        
        vector<pair<string, double>> posteriors = computeNormalizedPosteriors(likelihoods);
        
        cout << "Normalized Posteriors:" << endl;
        cout << fixed << setprecision(6);
        
        for (const auto& pair : posteriors) {
            cout << "  " << pair.first << ": " << pair.second << endl;
        }
        
        return 0;
    }
    
    // ========================================================================
    // UNKNOWN COMMAND
    // ========================================================================
    
    else {
        cerr << "Error: Unknown command: " << command << endl;
        printUsage(argv[0]);
        return 1;
    }
    
    return 0;
}
