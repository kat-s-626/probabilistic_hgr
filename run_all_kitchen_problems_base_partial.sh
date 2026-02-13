#!/bin/bash

# Get absolute path to current directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Number of parallel jobs per problem (default: 4, adjust based on your CPU cores)
MAX_JOBS=${MAX_JOBS:-4}

# Create results directory
mkdir -p results_base_partial_time

# Function to process a single observation count
process_observation() {
    local BASE_NAME=$1
    local OBS_COUNT=$2
    local PROBLEM_DIR=$3
    local NUM_ACTIONS=$4
    
    echo "  [Job $OBS_COUNT/$NUM_ACTIONS] Running with $OBS_COUNT observations"
    
    # Create timing file
    OBS_DIR="$PROBLEM_DIR/obs_$OBS_COUNT"
    mkdir -p "$OBS_DIR"
    
    # Start timing
    START_TIME=$(date +%s.%N)
    
    # Create unique work directory for this observation count
    WORK_DIR="$OBS_DIR/pgr_files"
    mkdir -p "$WORK_DIR"
    
    # Generate mtlt/tlt version of the problem file
    MTLT_PROBLEM="$OBS_DIR/${BASE_NAME}_mtlt.hddl"
    ./posterior_helper mtlt "benchmarks/kitchen-100/01-problems/$BASE_NAME.hddl" "$MTLT_PROBLEM" > /dev/null 2>&1
    
    if [ ! -f "$MTLT_PROBLEM" ]; then
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> Error: Failed to generate mtlt problem file"
        return 1
    fi
    
    # Convert to absolute paths
    ABS_OBS_DIR="$SCRIPT_DIR/$OBS_DIR"
    ABS_WORK_DIR="$SCRIPT_DIR/$WORK_DIR"
    ABS_MTLT_PROBLEM="$SCRIPT_DIR/$MTLT_PROBLEM"
    
    # Change to work directory to run pgr workflow
    cd "$ABS_WORK_DIR"
    
    # Run the PGR workflow manually (pgr.sh only takes 3 arguments)
    DOMAIN="$SCRIPT_DIR/benchmarks/kitchen-100/00-domain/domain_explicit_hypotheses.hddl"
    SOLUTION="$SCRIPT_DIR/benchmarks/kitchen-100/03-partial-solutions/$BASE_NAME.txt"
    
    # Copy solution file to work directory with a simple name
    cp "$SOLUTION" solution.txt
    
    # Parse
    "$SCRIPT_DIR/pandaPIparser" "$DOMAIN" "$ABS_MTLT_PROBLEM" temp.parsed > "$ABS_OBS_DIR/run.log" 2>&1
    if [ ! -f temp.parsed ]; then
        cd "$SCRIPT_DIR"
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> Error: Parsing failed"
        return 1
    fi
    
    # Ground
    "$SCRIPT_DIR/pandaPIgrounder" -q temp.parsed "domain-problem.psas" >> "$ABS_OBS_DIR/run.log" 2>&1
    rm temp.parsed
    if [ ! -f "domain-problem.psas" ]; then
        cd "$SCRIPT_DIR"
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> Error: Grounding failed"
        return 1
    fi
    
    # Generate PGR files for this specific observation count
    "$SCRIPT_DIR/build/pandaPIpgrRepairVerify" pgrpo "domain-problem.psas" "solution.txt" "$OBS_COUNT" >> "$ABS_OBS_DIR/run.log" 2>&1
    
    # Determine the PGR filename based on observation count
    if (( OBS_COUNT < 10 )); then
        pgrprob="solution.txt-00${OBS_COUNT}.pgr"
    elif (( OBS_COUNT < 100 )); then
        pgrprob="solution.txt-0${OBS_COUNT}.pgr"
    else
        pgrprob="solution.txt-${OBS_COUNT}.pgr"
    fi
    
    # If this is the full observation, use different naming
    if (( OBS_COUNT == NUM_ACTIONS )); then
        pgrprob="solution.txt-full.pgr"
    fi
    
    # Run planner on the generated PGR file
    if [ -f "$pgrprob" ]; then
        "$SCRIPT_DIR/pplanner" "$pgrprob" > "${pgrprob}.log" 2>&1
    else
        cd "$SCRIPT_DIR"
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> Error: PGR file not generated"
        return 1
    fi
    
    # Return to script directory
    cd "$SCRIPT_DIR"
    
    # End timing
    END_TIME=$(date +%s.%N)
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    
    # Log timing information
    echo "Elapsed time: $ELAPSED seconds" > "$OBS_DIR/timing.txt"
    echo "Start: $(date -d @${START_TIME%.*} '+%Y-%m-%d %H:%M:%S')" >> "$OBS_DIR/timing.txt"
    echo "End: $(date -d @${END_TIME%.*} '+%Y-%m-%d %H:%M:%S')" >> "$OBS_DIR/timing.txt"
    
    # Check if PGR generation completed successfully
    if [ -d "$WORK_DIR" ] && ls "$WORK_DIR"/*.pgr >/dev/null 2>&1; then
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> PGR files saved to $OBS_DIR (${ELAPSED}s)"
    else
        echo "    [Job $OBS_COUNT/$NUM_ACTIONS] -> Warning: PGR generation may have failed (${ELAPSED}s)"
    fi
}

export -f process_observation
export SCRIPT_DIR

# for each problem file in benchmarks/kitchen-100/01-problems
for PROBLEM_FILE in benchmarks/kitchen-100/01-problems/p-*.hddl; do
    # extract base name without path and extension
    BASE_NAME=$(basename "$PROBLEM_FILE" .hddl)
    
    # Skip _mtlt files (they are generated, not original problems)
    if [[ "$BASE_NAME" == *"_mtlt" ]]; then
        continue
    fi
    
    # Create directory for this problem
    PROBLEM_DIR="results_base_partial_time/$BASE_NAME"
    mkdir -p "$PROBLEM_DIR"

    echo "Running PGR solving for problem: $BASE_NAME"
    OBSERVATION_FILE="benchmarks/kitchen-100/03-partial-solutions/$BASE_NAME.txt"

    # Get number of actions in the solution
    NUM_ACTIONS=$(grep -o "([^()]*)" "$OBSERVATION_FILE" | wc -l | tr -d ' ')
    echo "  Number of actions in solution: $NUM_ACTIONS"
    echo "  Using $MAX_JOBS parallel jobs"
    
    # Start timing for entire problem
    PROBLEM_START=$(date +%s.%N)
    
    # Use GNU parallel or xargs for parallel execution
    if command -v parallel &> /dev/null; then
        # Use GNU parallel if available
        seq 0 $NUM_ACTIONS | parallel -j $MAX_JOBS process_observation $BASE_NAME {} "$PROBLEM_DIR" $NUM_ACTIONS
    else
        # Fallback to xargs with background jobs
        seq 0 $NUM_ACTIONS | xargs -P $MAX_JOBS -I {} bash -c "process_observation $BASE_NAME {} '$PROBLEM_DIR' $NUM_ACTIONS"
    fi
    
    # End timing for entire problem
    PROBLEM_END=$(date +%s.%N)
    PROBLEM_ELAPSED=$(echo "$PROBLEM_END - $PROBLEM_START" | bc)
    
    # Log overall problem timing
    echo "Total time for $BASE_NAME: ${PROBLEM_ELAPSED}s" | tee "$PROBLEM_DIR/total_timing.txt"
    echo ""

    # Clean up any temporary files if needed
done