#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <regex>
#include <cmath>
#include <map>
#include <iomanip>
#include <csignal>
#include <csetjmp>
#include <unistd.h>
#include <unordered_set>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;

#define DEBUG 1

// Global variables
string save_dir;
string log_file;
string num_obs_str;
string obs_file;
string domain_file;
string problem_file;
string problem_tlt_file = "problem_tlt.hddl";
int curr_iteration = 1;
string grounded_psas_suffix = "_grounded.psas";
string plan_log_suffix = "_obs_pgr.log";
string baseline_problem_suffix = "_baseline_problem.hddl";
string baseline_problem_file = "";
string curr_hypothesis = "";
string reduced_psas_suffix = "_reduced_grounded.psas";
string baseline_grounded_psas_suffix = "_baseline_grounded.psas";
string domain_reduced_suffix = "_domain_reduced.hddl";
string overall_likelihood_file;
map<string, double> iteration_likelihoods;
vector<string> iteration_order;
bool single_line_hypothesis = false;
string alt_hypothesis_parameters = "";

string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    size_t end = str.find_last_not_of(" \t\r\n");
    
    if (start == string::npos) return "";
    return str.substr(start, end - start + 1);
}

// Global variables for timeout handling
static jmp_buf timeout_jump;
static volatile sig_atomic_t timeout_occurred = 0;

void timeout_handler(int signum) {
    timeout_occurred = 1;
    longjmp(timeout_jump, 1);  // Jump back to setjmp location
}

int wrap_tlt(string problem_file) {
    // Add top level task to problem file, and comment out original tasks
    ifstream file(problem_file);
    if (!file.is_open()) {
        cerr << "Iteration " << curr_iteration << " - wrap_tlt(): Error: Cannot open problem file: " << problem_file << endl;
        return 1;
    }

    string content;
    string line;
    string output_file = save_dir + problem_tlt_file;

    #ifdef DEBUG
    cout << "Processing problem file: " << problem_file << endl;
    cout << "Output will be saved to: " << output_file << endl;
    cout << " " << endl;
    #endif
    
    
    // Process line by line
    while (getline(file, line)) {
        // First sed: uncomment if exactly matches
        regex pattern1(R"(^;; (\(:htn :tasks \(tlt\)\).*$))");
        smatch match;
        
        if (regex_search(line, match, pattern1)) {
            string uncommented_line = regex_replace(line, pattern1, "$1");
            content += uncommented_line + "\n";
            
            #ifdef DEBUG
            cout << "Uncommenting line: " << line << endl;
            cout << "Matched pattern: " << match[1].str() << endl;
            cout << "Uncommented line: " << uncommented_line << endl;
            cout << " " << endl;
            #endif
            continue;  // Move to next line after uncommenting
        }
        
        
        // Second sed: comment out other :htn :tasks lines (but not tlt ones)
        regex pattern2(R"(^\s*\(:htn :tasks .*$)");

        if (regex_search(line, regex(R"(^\s*\(:htn :tasks .*$)"))) {
            cout << "Commenting out line: " << line << endl;
            line = regex_replace(line, pattern2, ";;$1");

            #ifdef DEBUG
            cout << "Matched pattern: " << match[0].str() << endl;
            cout << "Commented line: " << line << endl;
            cout << " " << endl;
            #endif
        }
        content += line + "\n";
    }
    
    file.close();


    ofstream output(output_file);
    if (!output.is_open()) {
        cerr << "Iteration " << curr_iteration << " - wrap_tlt(): Error creating output file: " << output_file << endl;
        return 1;
    }
    
    output << content;
    output.close();

    problem_tlt_file = output_file;  // Update global variable to new file path
    
    #ifdef DEBUG
    cout << "Conversion complete!" << endl;
    cout << "Output written to: " << output_file << endl;
    cout << "Updated problem_tlt_file to: " << problem_tlt_file << endl;
    cout << " " << endl;
    #endif
    
    return 0;

}

int remove_high_level_task() {    
    ifstream file(domain_file);

    if (!file.is_open()) {
        cerr << "Iteration " << curr_iteration << " - remove_high_level_task(): Error: Cannot open domain file: " << domain_file << endl;
        return 1;
    }

    string content;
    string line;
    string output_file = save_dir + to_string(curr_iteration) + domain_reduced_suffix;

   
    regex high_level_task_pattern(R"(([\w-]+)\[([^\]]+)\])");
    smatch match;
    string target_tlt = "";
    if (regex_search(curr_hypothesis, match, high_level_task_pattern)) {
        string high_level_task = match[1].str();
        regex pattern(R"(^(\s*\(:method m-[\w-]+.*$))");
        line = regex_replace(line, pattern, ";;$1");
        target_tlt = "m-tlt-"  + high_level_task;

        #ifdef DEBUG
        cout << "High-level task identified from hypothesis: " << high_level_task << endl;
        cout << "Target TLT to remove: " << target_tlt << endl;
        #endif
    }



    bool within_block_to_remove = false;
    // Process line by line
    int comment_count = 6;
    while (getline(file, line)) {
        if (line.find("(:method " + target_tlt) != string::npos) {
            within_block_to_remove = true;
            #ifdef DEBUG
            cout << "Found start of method block to remove at line: " << line << endl;
            #endif
            
        }

        if (within_block_to_remove) {
            comment_count--;
            // Comment out this line
            #ifdef DEBUG
            cout << "Removing this line: " << line << endl;
            #endif

            if (comment_count <= 0) {
                within_block_to_remove = false;  // Safety check to avoid commenting out too many lines
                #ifdef DEBUG
                cout << "Reached comment limit, stopping commenting at line: " << line << endl;
                #endif
            }
        } else {
            content += line + "\n";
        }
    }
    
    ofstream output(output_file);
    if (!output.is_open()) {
        cerr << "Iteration " << curr_iteration << " - remove_high_level_task(): Error creating output file: " << output_file << endl;
        return 1;
    }

    output << content;
    output.close();

    // Update domain_file to the new reduced domain file
    domain_file = output_file;
    #ifdef DEBUG
    cout << "High-level task removal complete!" << endl;
    cout << "Output written to: " << output_file << endl;
    cout << "Updated domain_file to: " << domain_file << endl;
    cout << " " << endl;
    #endif

    return 0;
}

int reduce_psas_file(string psas_file_name){
    ifstream inFile(psas_file_name);
    if (!inFile.is_open()) {
        cerr << "Iteration " << curr_iteration << " - Error: Cannot open psas file: " << psas_file_name << endl;
        return 1;  // Should return 1, not false
    }
    
    vector<string> lines;
    vector<string> newlines;
    string line;
    while (getline(inFile, line)) {
        lines.push_back(line);
    }
    inFile.close();

    if (single_line_hypothesis) {
        // Pass 1: Find IDs to delete
        #ifdef DEBUG
        cout << "Finding IDs to delete for single-line hypothesis: " << curr_hypothesis << endl;
        #endif
        string reduced_psas_file = save_dir + to_string(curr_iteration) + reduced_psas_suffix;
        ofstream outFile(reduced_psas_file);
        if (!outFile.is_open()) {
            cerr << "Iteration " << curr_iteration << " - Error: Cannot open output file: " << reduced_psas_file << endl;
            return 1;
        }
        unordered_set<string> idsToDelete;
        unordered_set<int> linesToDelete;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find(curr_hypothesis)!= std::string::npos) {
                #ifdef DEBUG
                cout << "Marking ID for deletion: " << lines[i + 1] << " at line " << i + 1 << endl;
                #endif
                
                if (i + 1 < lines.size()) {
                    idsToDelete.insert(lines[i + 1]);
                    linesToDelete.insert(i);
                }
            }
        }

        // need to delete lines (i - 4 to i + 3)
        for (int lineNum : linesToDelete) {
            for (int j = lineNum - 4; j <= lineNum + 3; ++j) {
                if (j >= 0 && j < lines.size()) {
                    linesToDelete.insert(j);
                    #ifdef DEBUG
                    cout << "Also marking line " << j << " for deletion" << endl;
                    #endif
                }
            }
        }
        
        // Pass 2: Write output, skipping marked blocks
        #ifdef DEBUG
        cout << "IDs to delete: ";
        for (const auto& id : idsToDelete) {
            cout << id << " ";
        }
        cout << endl;
        #endif
        for (size_t i = 0; i < lines.size(); ++i) {
            if (find(linesToDelete.begin(), linesToDelete.end(), i) == linesToDelete.end()) {
                outFile << lines[i] << "\n";
            } else {
                #ifdef DEBUG
                cout << "Deleting line " << i << ": " << lines[i] << endl;
                #endif
            }
        }
        
        outFile.close();
        #ifdef DEBUG
        cout << "Reduced psas file written to: " << reduced_psas_file << endl;
        cout << "Removed " << (lines.size() - newlines.size()) << " lines" << endl;
        #endif
                
        return 0;
    }


    int skip_lines = 0;  // Simpler: count lines to skip remaining
    bool next_method_count = false;
    
    for (size_t i = 0; i < lines.size(); i++) {
        line = lines[i];
        
        if (skip_lines > 0) {
            // Currently skipping method block lines
            skip_lines--;
            #ifdef DEBUG
            cout << "Skipping line " << i << ": " << line << endl;
            #endif

            // if (single_line_hypothesis) {
            //     if (skip_lines == 1) {  // Keep the last line of the method block for single-line hypothesis
            //         //  line 4143 4142 18957 18526 4145 4144 -1 -> -1
            //         // regex number_pattern(R"((\d+) -1)");
            //         // smatch number_match;

            //         // if (regex_search(line, number_match, number_pattern)) {
            //         //     string modified_line = regex_replace(line, number_pattern, "-1");
            //         //     newlines.push_back(modified_line);
            //         //     #ifdef DEBUG
            //         //     cout << "Modified original line: " << line << " for single-line hypothesis at " << i << ": " << modified_line << endl;
            //         //     #endif
            //         // }
            //         newlines.push_back("-1");
            //     } else if (skip_lines == 0) {  // Add a new line with 6 0 before the -1 for single-line hypothesis
            //         // regex number_pattern_2(R"((\d+) -1)");
            //         // smatch number_match_2;
                    
            //         // if (regex_search(line, number_match_2, number_pattern_2)) {
            //         //     string modified_line = regex_replace(line, number_pattern_2, "-1");
            //         //     newlines.push_back(modified_line);
            //         //     #ifdef DEBUG
            //         //     cout << "Modified line for single-line hypothesis at " << i << ": " << modified_line << endl;
            //         //     #endif
            //         // }
            //         newlines.push_back("-1");
            //     } else if (skip_lines == 2) {  // Add the line with 6 0 for single-line hypothesis
            //         newlines.push_back(line);
            //     } else {
            //         newlines.push_back("99999");
            //     }
                    
            //     }
            continue;
        }
         if (next_method_count) {
            // This line should be the method count line, which we can skip and then start skipping the method block
            next_method_count = false;
            int method_count = stoi(line) - 1;
            #ifdef DEBUG
            cout << "Original method count: " << line << ", reduced method count: " << method_count << endl;
            #endif
            line = to_string(method_count);

            #ifdef DEBUG
            cout << "Modified method count line at " << i << ": " << line << endl;
            #endif
        }


        if (line.find(";; methods") != string::npos) {
            // Next line(s) will be method block for curr_hypothesis, so start skipping
            next_method_count = true;
            #ifdef DEBUG
            cout << "Found method count line at " << i << ": " << line << endl;
            #endif

        }
       
        
        // Check if this line starts a method block for curr_hypothesis
        string target_hypothesis_str = curr_hypothesis;
        if (line.find(target_hypothesis_str) != string::npos) {
            #ifdef DEBUG
            cout << "Found hypothesis " << target_hypothesis_str << " at line " << i << ": " << line << endl;
            #endif
            skip_lines = 3;  // Skip this line and the next 3 lines (method block)
            

            if (single_line_hypothesis) {
                newlines.push_back(line);  // Keep the original line for single-line hypothesis
            }

            continue;
        }
        
        // Keep this line
        newlines.push_back(line);
    }
    
    // Write ONCE after processing all lines
    string reduced_psas_file = save_dir + to_string(curr_iteration) + reduced_psas_suffix;
    ofstream outFile(reduced_psas_file);
    if (!outFile.is_open()) {
        cerr << "Iteration " << curr_iteration << " - Error: Cannot write to output file: " << reduced_psas_file << endl;
        return 1;
    }
    
    for (const string& outLine : newlines) {
        outFile << outLine << endl;
    }
    outFile.close();
    
    #ifdef DEBUG
    cout << "Reduced psas file written to: " << reduced_psas_file << endl;
    cout << "Removed " << (lines.size() - newlines.size()) << " lines" << endl;
    #endif
    
    return 0;
}

// For parsing and grounding psas files
int step_1() {
    #ifdef DEBUG
    cout << "======================================" << endl;
    cout << "Step 1: Parsing and grounding PSAS files" << endl;
    cout << "Domain file: " << domain_file << endl;
    cout << "Problem TLT file: " << problem_tlt_file << endl;
    cout << "Iteration: " << curr_iteration << endl;
    cout << "======================================" << endl;
    #endif

    string parsed_htn_file = save_dir + to_string(curr_iteration) + "_parsed.htn";
    string parser_log = save_dir + to_string(curr_iteration) + "_parser.log";
    string ground_log = save_dir + to_string(curr_iteration) + "_ground.log";
    string grounded_psas_file = save_dir + to_string(curr_iteration) + grounded_psas_suffix;
    
    #ifdef DEBUG
        cout << "parsed_htn_file: " << parsed_htn_file << endl;
        cout << "Parser log: " << parser_log << endl;
        cout << "Ground log: " << ground_log << endl;
        cout << "Grounded PSAS file: " << grounded_psas_file << endl;
    #endif

    // Parse to htn model with pandaPIparser (use global domain_file directly)
    #ifdef DEBUG
    cout << " " << endl;
    cout << "./pandaPIparser \"" << domain_file << "\" \"" << problem_tlt_file << "\" \"" << parsed_htn_file << "\"" << " > " << parser_log << " 2>&1" << endl;
    #endif

    system(("./pandaPIparser \"" + domain_file + "\" \"" + problem_tlt_file + "\" \"" + parsed_htn_file + "\"" + " > " + parser_log + " 2>&1").c_str());

    #ifdef DEBUG
    cout << "Completed parsing for iteration " << curr_iteration << endl;
    cout << " " << endl;
    #endif

    // parse htn to psas with pandaPIgrounder
    system(("./pandaPIgrounder -q \"" + parsed_htn_file + "\" \"" + grounded_psas_file + "\"" + " >> " + ground_log + " 2>&1").c_str());
    #ifdef DEBUG
    cout << "./pandaPIgrounder -q \"" << parsed_htn_file << "\" \"" << grounded_psas_file << "\"" << " >> " + ground_log + " 2>&1" << endl;
    cout << "Completed grounding for iteration " << curr_iteration << endl;
    cout << " " << endl;
    #endif
    

    return 0;
}

// For creating observation-enforcing pgr file
int step_2(bool is_full_observation = true, bool is_full_obs = false){
    #ifdef DEBUG
    cout << "======================================" << endl;
    cout << "Step 2: Generating observation-enforcing PGR file" << endl;
    cout << "Observation file: " << obs_file << endl;
    cout << "=======================================" << endl;
    cout << endl;
    #endif

    string obs_mode;
    if (is_full_observation) {
        obs_mode = "pgrfo";
    } else {
        obs_mode = "pgrpo";
    }

    // Generate pgr file with observations using htnPrefixEncoding
    string pgr_gen_log = save_dir + to_string(curr_iteration) + "_pgr_gen.log";
    string pgr_output = save_dir + to_string(curr_iteration) + "_obs.pgr";
    string grounded_psas = save_dir + to_string(curr_iteration) + grounded_psas_suffix;
    // string reduced_psas = save_dir + to_string(curr_iteration) + reduced_psas_suffix;

    ifstream grounded_check(grounded_psas);

    // if (!is_first_iteration) {
    //     string reduced_psas = save_dir + to_string(curr_iteration - 1) + reduced_psas_suffix;

    //     #ifdef DEBUG
    //         cout << "Checking for grounded psas: " << grounded_psas << endl;
    //         cout << "Checking for reduced psas: " << reduced_psas << endl;
    //     #endif
    //     ifstream reduced_check(reduced_psas);
    //     if (reduced_check.good()) {
    //         system(("cp \"" + reduced_psas + "\" \"" + grounded_psas + "\"").c_str());
    //         #ifdef DEBUG
    //             cout << "Using reduced psas for iteration " << curr_iteration << ": " << reduced_psas << endl;
    //         #endif
    //     } else {
    //         cerr << "Error: Missing reduced psas file for iteration " << curr_iteration << endl;
    //         return 1;
    //     }
    // }

    // Before Encoding, check if pgr file already exist first, if so, delete it

    // The pgr file is located in the same directory as observations.txt, move to the save_dir
    // The pgr file is named: observations.txt-XXX.pgr where XXX is zero-padded num_obs
    string obs_basename = obs_file.substr(obs_file.find_last_of("/") + 1);
    char num_obs_padded[4];
    string generated_pgr;
    if (is_full_observation) {
        snprintf(num_obs_padded, sizeof(num_obs_padded), "%03d", stoi(num_obs_str));
        generated_pgr = obs_file.substr(0, obs_file.find_last_of("/") + 1) + 
                        obs_basename + "-" + string(num_obs_padded) + ".pgr";
    } else {
        // full observation is "full" instead of number
        generated_pgr = obs_file.substr(0, obs_file.find_last_of("/") + 1) + 
                        obs_basename + "-full.pgr";
        
    }

    if (remove(generated_pgr.c_str()) != 0) {
        #ifdef DEBUG
            cout << "Removed existing PGR file: " << generated_pgr << endl;
        #endif
    } else {
        #ifdef DEBUG
            cout << "No existing PGR file to remove: " << generated_pgr << endl;
        #endif
    }

    int pgr_ret = system(("./htnPrefixEncoding \"" + 
        obs_mode + 
        "\" \"" + grounded_psas + 
        "\" \"" + obs_file + 
        "\" " + num_obs_str + 
        " > " + pgr_gen_log + " 2>&1").c_str());
    #ifdef DEBUG
        cout << "htnPrefixEncoding command: " << "./htnPrefixEncoding \"" + 
        obs_mode + 
        "\" \"" + grounded_psas + 
        "\" \"" + obs_file + 
        "\" " + num_obs_str + 
        " > " + pgr_gen_log + " 2>&1" << endl;
        cout << "htnPrefixEncoding return code: " << pgr_ret << endl;
        cout << "Completed htnPrefixEncoding for iteration " << curr_iteration << endl;
    #endif

    ifstream pgr_check(generated_pgr);
    if (!pgr_check.good()) {
        cerr << "Iteration " << curr_iteration << " - step_2() Error: Generated PGR file not found: " << generated_pgr << endl;
        return 1;
    } else {
        #ifdef DEBUG
            cout << "Generated PGR file found for iteration " << curr_iteration << ": " << generated_pgr << endl;
        #endif
    }
    pgr_check.close();

    string move_cmd = "cp \"" + generated_pgr + "\" \"" + pgr_output + "\"";

    #ifdef DEBUG
        cout << "Generated PGR file: " << generated_pgr << endl;
        cout << "Moving to: " << pgr_output << endl;
    #endif

    system(move_cmd.c_str());
    return 0;
}

// Generate plan for the observation-enforcing problem
int step_3(){
    #ifdef DEBUG
    cout << "======================================" << endl;
    cout << "Step 3: Generating plan for observation-enforcing problem" << endl;
    cout << "Observation-enforcing PGR file: " << save_dir + to_string(curr_iteration) + "_obs.pgr" << endl;
    cout << "=======================================" << endl;
    #endif

    system(("./pplanner \"" + save_dir + to_string(curr_iteration) + "_obs.pgr\" > \"" + save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log\" 2>&1").c_str());
    // Plan saved in log file
    #ifdef DEBUG
        cout << "Generated plan for observation-enforcing problem, saved in log file: " << save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log" << endl;
        ifstream file(save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log");
        bool in_section = false;
        string line;
        
        while (getline(file, line)) {
            if (line.find("==>") != string::npos) {
                in_section = true;
                continue;
            }
            if (line.find("<==") != string::npos) {
                break;
            }
            if (in_section) {
                cout << line << endl;
            }
        }

        if (!in_section) {
            cerr << "Iteration " << curr_iteration << " - Warning: No plan section found in log: " << save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log" << endl;
        }
    #endif
    return 0;
}

// Extract hypothesis from obs_pgr.log file
int step_4(){
    #ifdef DEBUG
    cout << "======================================" << endl;
    cout << "Step 4: Extracting hypothesis from obs_pgr.log file" << endl;
    cout << "Observation-enforcing PGR log file: " << save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log" << endl;
    cout << "=======================================" << endl;
    #endif
    
    // First: parse  the line "0 __top[] -> __top_method {num}" and get the id number at the end, which corresponds to the method encoding of the hypothesis
    ifstream file(save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log");
    int top_task_id = -1;
    if (!file.is_open()) {
        cerr << "Iteration " << curr_iteration << " - step_4() Error: Cannot open log file: " << save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log" << endl;
        return 1;
    }

    // If found "- Status: Proven unsolvable" in the last line of the log file, then skip the rest of this iteration and move to next iteration (cleanup)
    ifstream logFile(save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log");
    if (logFile.good()) {
        string lastLine;
        string line;
        while (getline(logFile, line)) {
            lastLine = line;
        }
        if (lastLine.find("Status: Proven unsolvable") != string::npos) {
            #ifdef DEBUG
            cout << "Plan generation failed with 'Proven unsolvable'. Skipping hypothesis extraction and moving to next iteration." << endl;
            #endif
            logFile.close();
            file.close();
            return 2;  // Not an error, just skip to next iteration
        }
    }

    string line;
    map<int, string> id_to_method;

    while (getline(file, line)) {
        // if line starts with a number followed by a space, then parse the id and method encoding and save in map
        if (regex_search(line, regex(R"(^\d+ .*$)"))) {
            size_t first_space = line.find(' ');
            int id = stoi(line.substr(0, first_space));
            string method_encoding = line.substr(first_space + 1);
            id_to_method[id] = method_encoding;
        }

         if (line.find("__top[] ->") != string::npos) {
             // Find last space
            size_t last_space = line.find_last_of(' ');
            
            // Extract substring after last space
            string number_str = line.substr(last_space + 1);
            
            // Convert to int
            top_task_id = stoi(number_str);
            
            #ifdef DEBUG
            cout << "Final number: " << top_task_id << endl;
            cout <<" Items in id_to_method map: " << id_to_method.size() << endl;
            #endif
            break;
        }
    }

    
    // parse the hypothesis string
    regex pattern(R"((?:^|;| )([\w-]+\[[^\]]+\])(?:;|\s))");
    
    if (id_to_method.find(top_task_id) != id_to_method.end()) {
        string method_encoding = id_to_method[top_task_id];

        #ifdef DEBUG
        cout << "Found method encoding for top task id " << top_task_id << ": " << method_encoding << endl;    
        cout << "Method encoding for top task: " << method_encoding << endl;
        #endif

        smatch match;
        if (regex_search(method_encoding, match, pattern)) {
            single_line_hypothesis = false;
            curr_hypothesis = match[1].str();
            #ifdef DEBUG
            cout << "Extracted hypothesis: " << curr_hypothesis << endl;
            #endif
        } else {
            // Alternative format: hypothesis is one line upper and looks like 13 tlt[] -> m-tlt-plow-road 2329 (without [])
            // get the last number from the method encoding line
            single_line_hypothesis = true;
            
            #ifdef DEBUG
            cout << "Alternative hypothesis: " << method_encoding << endl;
            #endif
            
            regex alt_pattern(R"(-> ([a-z\-]+) \d+)");
            smatch alt_match;

            // get the number at the end of the method encoding line
            regex number_pattern(R"(\d+$)");
            smatch number_match;
            if (regex_search(method_encoding, number_match, number_pattern)) {
                alt_hypothesis_parameters = id_to_method[stoi(number_match[0].str())];
                
                #ifdef DEBUG
                cout << "Alternative method encoding: " << alt_hypothesis_parameters << endl;
                #endif  
                
                regex method_pattern(R"(([a-z0-9\-_]+\[[a-z\-,]+\]))");
                smatch method_match;
                if (regex_search(alt_hypothesis_parameters, method_match, method_pattern)) {
                    curr_hypothesis = method_match[1].str();
                    #ifdef DEBUG
                    cout << "Extracted alternative hypothesis parameters as curr_hypothesis: " << curr_hypothesis << endl;
                    #endif
                } else {
                    cerr << "Iteration " << curr_iteration << " - step_4() Error: No method encoding found for alternative hypothesis parameters in map" << endl;
                }

                curr_hypothesis = alt_hypothesis_parameters;
            }
        }
    }
    
    file.close();
    if (top_task_id == -1) {
        cerr << "Iteration " << curr_iteration << " - step_4() Error: No __top[] -> line found in log file" << endl;
        return 1;
    }

    return 0;
}

int step_5(){
    
    // transform hypothesis to predicate form (e.g. set-up-shelter[mendon-pond] (set-up-shelter mendon-pond))
    #ifdef DEBUG
    cout << "======================================" << endl;
    cout << "Step 5: Transforming hypothesis to predicate form" << endl;
    cout << "Current hypothesis: " << curr_hypothesis << endl;
    cout << "Alternative hypothesis parameters: " << alt_hypothesis_parameters << endl;
    cout << "Single line hypothesis: " << (single_line_hypothesis ? "true" : "false") << endl;
    cout << "=======================================" << endl;
    #endif

    regex pattern(R"(([\w-]+)\[([^\]]+)\])");
    smatch match;
    string predicate_str;
    string target_hypothesis = single_line_hypothesis ? alt_hypothesis_parameters : curr_hypothesis;

    if (regex_search(target_hypothesis, match, pattern)) {
        string predicate = match[1].str();
        string args_str = match[2].str();
        // Split args by comma
        vector<string> args;
        stringstream ss(args_str);
        string arg;
        while (getline(ss, arg, ',')) {
            args.push_back(arg);
        }
        // Construct predicate string
        predicate_str = "(" + predicate;
        for (const auto& a : args) {
            predicate_str += " " + a;
        }
        predicate_str += ")";
        
        #ifdef DEBUG
        cout << "Transformed hypothesis: " << predicate_str << endl;
        #endif
    }

    ifstream inFile(problem_file);
    if (!inFile.is_open()) {
        cerr << "Iteration " << curr_iteration << " - step_5() Error: Cannot open template file: " << problem_file << endl;
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
        string trimmed_line = trim(line);
        
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
                regex pattern(R"((:htn :tasks )\([^)]+\))");
                string result = regex_replace(line, pattern, "$1" + predicate_str);
                newLines.push_back(result);

                #ifdef DEBUG
                    cout << "Original line: " << line << endl;
                    cout << "Modified line: " << result << endl;
                #endif
                
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
        if (inHtnSection && trimmed_line.find(":ordering") != string::npos) {
            inHtnSection = false;
        }
        
        // Default: keep line as-is
        newLines.push_back(line);
    }
    
    // Write output file
    baseline_problem_file = save_dir + to_string(curr_iteration) + baseline_problem_suffix;
    ofstream outFile(baseline_problem_file);
    if (!outFile.is_open()) {
        cerr << "Iteration " << curr_iteration << " - step_5() Error: Cannot write to output file: " << baseline_problem_file << endl;
        return false;
    }
    
    for (const string& outLine : newLines) {
        outFile << outLine << "\n";
    }
    outFile.close();
    
    #ifdef DEBUG
    cout << "Transformed problem file written to: " << baseline_problem_file << endl;
    cout << " " << endl;
    #endif

    return 0;
}

// Solve the baseline (unconstrained problem) 
int step_6(){
    
    #ifdef DEBUG
    cout << "======================================" << endl;
    cout << "Step 6: Solving the baseline (unconstrained) problem" << endl;
    cout << "Domain file: " << domain_file << endl;
    cout << "Baseline problem file: " << baseline_problem_file << endl;
    cout << "Current iteration: " << curr_iteration << endl;
    cout << "=======================================" << endl;
    cout << endl;
    #endif
    
    string baseline_parsed_htn_file = save_dir + to_string(curr_iteration) + "_baseline_parsed.htn";
    string baseline_parser_log = save_dir + to_string(curr_iteration) + "_baseline_parser.log";
    string baseline_ground_log = save_dir + to_string(curr_iteration) + "_baseline_grounded.log";
    
    // Parse to htn model with pandaPIparser
    system(("./pandaPIparser \"" + domain_file + "\" \"" + baseline_problem_file + "\" \"" + baseline_parsed_htn_file + "\"" + " > " + baseline_parser_log + " 2>&1").c_str());
    #ifdef DEBUG
    cout << "Parsed baseline problem to HTN model: " << baseline_parsed_htn_file << endl;
    cout << "Parser log: " << baseline_parser_log << endl;
    #endif  
    
    // parse htn to psas with pandaPIgrounder
    string baseline_grounded_psas_file = save_dir + to_string(curr_iteration) + baseline_grounded_psas_suffix;
    system(("./pandaPIgrounder -q \"" + baseline_parsed_htn_file + "\" \"" + baseline_grounded_psas_file + "\"" + " >> " + baseline_ground_log + " 2>&1").c_str());
    #ifdef DEBUG
    cout << "Grounded baseline problem to PSAS: " << baseline_grounded_psas_file << endl;
    cout << "Ground log: " << baseline_ground_log << endl;
    #endif
    
    string baseline_plan_log = save_dir + to_string(curr_iteration) + "_baseline.log";
    int plan_ret =system(("./pplanner \"" + baseline_grounded_psas_file + "\" > \"" + baseline_plan_log + "\" 2>&1").c_str());
    #ifdef DEBUG
    cout << "Generated plan for baseline problem, saved in log file: " << baseline_plan_log << endl;
    cout << "Plan return code: " << plan_ret << endl;
    #endif
    
    // Check if planning succeeded
    if (plan_ret != 0) {
        #ifdef DEBUG
        cout << "Planning failed for baseline problem. Check log file: " << baseline_plan_log << endl;
        #endif
        string grounded_psas_file = save_dir + to_string(curr_iteration) + grounded_psas_suffix;
        // reduce_psas_file(grounded_psas_file);
    }

    return 0;
}



// Calculate likelihood of the hypothesis based on the observation plan and baseline plan, and save to file
int step_7(){
    #ifdef DEBUG
    cout << "======================================" << endl;
    cout << "Step 7: Calculating likelihood of the hypothesis based on the observation plan and baseline plan" << endl;
    cout << "Current hypothesis: " << (single_line_hypothesis ? alt_hypothesis_parameters : curr_hypothesis) << endl;
    cout << "Observation plan log: " << save_dir + to_string(curr_iteration) + "_obs_pgr.log" << endl;
    cout << "Baseline plan log: " << save_dir + to_string(curr_iteration) + "_baseline.log" << endl;
    cout << "=======================================" << endl;
    #endif
    
    string baseline_grounded_psas_file = save_dir + to_string(curr_iteration) + baseline_grounded_psas_suffix;
    string obs_plan_log = save_dir + to_string(curr_iteration) + "_obs_pgr" + ".log";
    string baseline_plan_log = save_dir + to_string(curr_iteration) + "_baseline.log";
    string likelihood_file = save_dir + to_string(curr_iteration) + "_likelihoods.txt";
    int plan_ret = system(("./compute_normalized_likelihood \"" + baseline_grounded_psas_file + "\" \"" + obs_plan_log + "\" \"" + baseline_plan_log + "\" >> " + likelihood_file + " 2>&1").c_str());
    if (plan_ret != 0) {
        cerr << "Iteration " << curr_iteration << " - step_7() Error: Failed to compute likelihood. Check logs for details." << endl;
        return 1;
    }

    // get the likelihood value from the likelihood file (last line)
    ifstream likelihoodFile(likelihood_file);
    if (!likelihoodFile.is_open()) {
        cerr << "Iteration " << curr_iteration << " - step_7()  Error: Cannot open likelihood file: " << likelihood_file << endl;
        return 1;
    }
    string line;
    string lastLine;
    while (getline(likelihoodFile, line)) {
        if (line.find("P̂(ô | N^g, s_0) = ") != string::npos) {
            lastLine = line;
            break;
        }
    }
    likelihoodFile.close();

    // Write the likelihood to the overall likelihoods file with the hypothesis
    ofstream overallFile(overall_likelihood_file, ios::app);
    if (!overallFile.is_open()) {
        cerr << "Iteration " << curr_iteration << " - step_7() Error: Cannot write to overall likelihood file: " << overall_likelihood_file << endl;
        return 1;
    }

    string hyp_str = single_line_hypothesis ? alt_hypothesis_parameters : curr_hypothesis;
    overallFile << "Hypothesis: " << hyp_str << ", Likelihood:" << lastLine << endl;
    overallFile.close();

    // save the hypothesis and likelihood in a map for later writing final results
    iteration_likelihoods[hyp_str] = stod(lastLine.substr(lastLine.find("= ") + 2));
    iteration_order.push_back(hyp_str);

    #ifdef DEBUG
    cout << "Computed likelihood for hypothesis: " << hyp_str << endl;
    cout << "Likelihood value: " << iteration_likelihoods[hyp_str] << endl;
    cout << "Saved to overall likelihood file: " << overall_likelihood_file << endl;
    cout << endl;
    #endif
    
    return 0;
}

int step_8(){
    cout << "Step 8: Cleanup and prepare for next iteration" << endl;
    
    // Remove hypothesis from problem file for next iteration
    
    // reduce_psas_file(save_dir + to_string(curr_iteration) + grounded_psas_suffix);

    remove_high_level_task();
    return 0;
}

int write_final_results() {
    ofstream outFile(overall_likelihood_file, ios::app);
    if (!outFile.is_open()) {
        cerr << "Iteration " << curr_iteration << " - write_final_results()Error: Cannot write to overall likelihood file: " << overall_likelihood_file << endl;
        return 1;
    }

    outFile << "============================================================" << endl;
    outFile << "Results by Iteration Order (Discovery Order)" << endl;
    outFile << "============================================================" << endl;
    outFile << endl;

    for (size_t i = 0; i < iteration_order.size(); i++) {
        const string& hypothesis = iteration_order[i];
        double likelihood = iteration_likelihoods[hypothesis];
        outFile << "Iteration " << (i + 1) << ": " << hypothesis << endl;
        outFile << "  Likelihood: " << scientific << setprecision(10) << likelihood << endl;
        outFile << endl;
    }

    double total_likelihood = 0.0;
    for (const auto& entry : iteration_likelihoods) {
        total_likelihood += entry.second;
    }

    vector<pair<string, double>> ranked;
    ranked.reserve(iteration_likelihoods.size());
    for (const auto& entry : iteration_likelihoods) {
        double posterior = (total_likelihood > 0.0) ? (entry.second / total_likelihood) : 0.0;
        ranked.push_back({entry.first, posterior});
    }

    stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });

    outFile << "============================================================" << endl;
    outFile << "Results Ranked by Posterior (Sorted by Probability)" << endl;
    outFile << "============================================================" << endl;
    outFile << endl;

    for (size_t i = 0; i < ranked.size(); i++) {
        const string& hypothesis = ranked[i].first;
        double likelihood = iteration_likelihoods[hypothesis];
        double posterior = ranked[i].second;
        outFile << "Rank " << (i + 1) << ": " << hypothesis << endl;
        outFile << "  Likelihood: " << scientific << setprecision(10) << likelihood << endl;
        outFile << "  Posterior:  " << scientific << setprecision(10) << posterior << endl;
        outFile << endl;
    }

    outFile.close();
    return 0;
}

int remove_all_files_starts_with_number() {
    string cmd = "find \"" + save_dir + "\" -type f -name '[0-9]*' -delete";
    return system(cmd.c_str());
}

void run_all_steps(bool is_all_obs){
    // Step 1: Parse and ground PSAS files
    step_1();
    
    // Step 2: Generate observation-enforcing PGR file
    step_2(true, is_all_obs);
    
    // Step 3: Generate plan for the observation-enforcing problem
    step_3();
    
    // Step 4: Extract hypothesis from obs_pgr.log file
    int outcome = step_4();
    if (outcome == 2) {
        return;  // Skip to next iteration if proven unsolvable
    }
    
    // Step 5: Transform hypothesis to predicate form
    step_5();
    
    // Step 6: Solve the baseline (unconstrained) problem
    step_6();
    
    // Step 7: Calculate likelihood of the hypothesis based on the observation plan and baseline plan
    step_7();
    
    // Step 8: Cleanup and prepare for next iteration
    step_8();
}


int main(int argc, char* argv[]) {
    string k_iterations_str;
    
    if (argc != 7) {
        cout << "Usage: " << argv[0] << " <domain_file> <problem_file> <observation_file> <num_obs> <k_iterations> <save_dir>" << endl;
        
        // Using default values for testing
        domain_file = "benchmarks/monroe-100/00-domain/domain.hddl";
        problem_file = "benchmarks/monroe-100/01-problems/p-0028-set-up-shelter.hddl";
        obs_file = "benchmarks/monroe-100/02-solutions/solution-0028.txt";
        num_obs_str = "2";
        k_iterations_str = "5";
        save_dir = "monroe_full_0028_" + num_obs_str + "_" + k_iterations_str;
    } else {
        domain_file = argv[1];
        problem_file = argv[2];
        obs_file = argv[3];
        num_obs_str = argv[4];
        k_iterations_str = argv[5];
        save_dir = argv[6];
    }
    

    auto now = chrono::system_clock::now();
    auto timestamp = chrono::duration_cast<chrono::seconds>(
        now.time_since_epoch()
    ).count();
    // save_dir = save_dir + to_string(timestamp);
    save_dir = save_dir + "/";

    // Create output directory
    string mkdir_cmd = "mkdir -p " + save_dir;
    system(mkdir_cmd.c_str());

    log_file = save_dir + "run_log.txt";
    string error_log_file = save_dir + "error_log.txt";

    // write all console output to log file
    freopen(log_file.c_str(), "w", stdout);
    freopen(error_log_file.c_str(), "w", stderr);
    
    #ifdef DEBUG
        cout << "===================== Input Parameters ====================" << endl;
        cout << "Domain file: " << domain_file << endl;
        cout << "Problem file: " << problem_file << endl;
        cout << "Observation file: " << obs_file << endl;
        cout << "Number of observations: " << num_obs_str << endl;
        cout << "Number of iterations: " << k_iterations_str << endl;
        cout << "Save directory: " << save_dir << endl;
        cout << "============================================================" << endl;
        cout << endl;
    #endif

    overall_likelihood_file = save_dir + "overall_likelihoods.txt";
    map<int, int> time_per_iteration;

    // 0: Preprocessing by setting up the top level task in the problem file

    #ifdef DEBUG
        cout << "Step 0: Preprocessing problem file: " << problem_file << endl;
        cout << endl;
    #endif
    string problem_tlt_path = save_dir + problem_tlt_file;
    wrap_tlt(problem_file);

    // Count the number of predicates in the solution file
    #ifdef DEBUG
        cout << "Counting number of primitive tasks in observation file: " << obs_file << endl;
    #endif

    ifstream obsFile(obs_file);
    string input((istreambuf_iterator<char>(obsFile)), istreambuf_iterator<char>());
    int num_predicates = count(input.begin(), input.end(), '(');
    obsFile.close();

    #ifdef DEBUG
        cout << "Number of primitive tasks in observation file: " << num_predicates << endl;
        cout << "============================================================" << endl;
        cout << endl;
    #endif

    const int iteration_time_limit = 500;
    // Set up signal handler for timeout


    for (curr_iteration = 1; curr_iteration <= stoi(k_iterations_str); curr_iteration++) {
        cout << "==================== Iteration " << curr_iteration << " ====================" << endl;

        auto start_time = chrono::high_resolution_clock::now();


        
        step_1();
        step_2(true, stoi(num_obs_str) == num_predicates);
        step_3();
        int outcome = step_4();
        if (outcome == 2) {
            break;
        }
        step_5();
        step_6();
        step_7();
        step_8();

        
        
        auto end_time = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::seconds>(end_time - start_time).count();
        time_per_iteration[curr_iteration] = duration;
        
        cout << "Iteration " << curr_iteration << " took " << duration << " seconds" << endl;
    }


    // Print time per iteration
    cout << "==================== Time per Iteration ====================" << endl;
    int total_time = 0;
    for (const auto& entry : time_per_iteration) {
        cout << "Iteration " << entry.first << ": " << entry.second << " seconds" << endl;
        total_time += entry.second;
    }

    cout << "Total Time: " << total_time << " seconds" << endl;
    write_final_results();
    remove_all_files_starts_with_number();

    return 0;

}
