#!/bin/bash

################################################################################
# Posterior Estimation via Iterative Hypothesis Selection
#
# Implements the workflow for computing posterior distribution P(N^g | ô, s_0)
# by iteratively:
#   1. Solving observation-enforcing problem with remaining hypotheses
#   2. Extracting selected hypothesis from log
#   3. Computing baseline (unconstrained) for that hypothesis
#   4. Computing normalized likelihood
#   5. Removing hypothesis from domain and repeating
#
# Usage: ./compute_posterior.sh <domain_file> <problem_file> <observation_file> <num_obs> <k_iterations>
################################################################################

set -e  # Exit on error

# Check arguments
if [ $# -lt 5 ]; then
    echo "Usage: $0 <domain_file> <problem_file> <observation_file> <num_obs> <k_iterations> [work_dir]"
    echo ""
    echo "Arguments:"
    echo "  domain_file     : HDDL domain file with hypothesis encoding"
    echo "  problem_file    : HDDL problem file with dummy top-level goal"
    echo "  observation_file: Text file with observation plan"
    echo "  num_obs         : Number of observations to use"
    echo "  k_iterations    : Number of hypotheses to select (k)"
    echo "  work_dir        : (optional) Working directory for output"
    echo ""
    echo "Example:"
    echo "  ./compute_posterior.sh benchmarks/kitchen-100/00-domain/domain_explicit_hypotheses.hddl benchmarks/kitchen-100/01-problems/p-0003-kitchen.hddl benchmarks/kitchen-100/02-solutions/p-0003-kitchen.txt 5 5"
    exit 1
fi

DOMAIN_FILE="$1"
PROBLEM_FILE="$2"
OBSERVATION_FILE="$3"
NUM_OBS="$4"
K_ITERATIONS="$5"
WORK_DIR="${6:-posterior_estimation_$(date +%Y%m%d_%H%M%S)}"

# Check required tools
for tool in pandaPIparser pandaPIgrounder pandaPIpgrRepairVerify pplanner; do
    if [ ! -f "./$tool" ]; then
        echo "Error: Required tool not found: $tool"
        echo "Please ensure PANDA tools are in the current directory"
        exit 1
    fi
done

# Check input files
for file in "$DOMAIN_FILE" "$PROBLEM_FILE" "$OBSERVATION_FILE"; do
    if [ ! -f "$file" ]; then
        echo "Error: Input file not found: $file"
        exit 1
    fi
done

# Setup working directory (already set from args or default)
mkdir -p "$WORK_DIR"

echo "============================================================"
echo "Posterior Estimation via Iterative Hypothesis Selection"
echo "============================================================"
echo ""
echo "Configuration:"
echo "  Domain:       $DOMAIN_FILE"
echo "  Problem:      $PROBLEM_FILE"
echo "  Observations: $OBSERVATION_FILE"
echo "  Num obs:      $NUM_OBS"
echo "  K iterations: $K_ITERATIONS"
echo "  Work dir:     $WORK_DIR"
echo ""

# Copy original files to work directory
cp "$DOMAIN_FILE" "$WORK_DIR/domain_original.hddl"
cp "$PROBLEM_FILE" "$WORK_DIR/problem_original.hddl"
cp "$OBSERVATION_FILE" "$WORK_DIR/observations.txt"

# Get base names
DOMAIN_BASE=$(basename "$DOMAIN_FILE" .hddl)
PROBLEM_BASE=$(basename "$PROBLEM_FILE" .hddl)

# Array to store results
declare -a HYPOTHESES
declare -a LIKELIHOODS

# File to store likelihoods for C++ processing
LIKELIHOODS_FILE="$WORK_DIR/likelihoods.txt"
> "$LIKELIHOODS_FILE"  # Clear file


# run pgr.sh to get observation-enforcing PGR file with dummy goal
./posterior_helper mtlt "$PROBLEM_FILE" "$WORK_DIR/problem_mtlt.hddl" > /dev/null 2>&1

PROBLEM_MTLT="$WORK_DIR/problem_mtlt.hddl"

# Main iteration loop
for iteration in $(seq 1 $K_ITERATIONS); do
    echo ""
    echo "============================================================"
    echo "ITERATION $iteration / $K_ITERATIONS"
    echo "============================================================"
    
    ITER_PREFIX="$WORK_DIR/iter_$(printf '%02d' $iteration)"
    
    # ========================================================================
    # STEP 1: Create domain file for this iteration
    # ========================================================================
    
    if [ $iteration -eq 1 ]; then
        # First iteration: use original domain
        CURRENT_DOMAIN="$WORK_DIR/domain_original.hddl"
        cp "$CURRENT_DOMAIN" "${ITER_PREFIX}_domain.hddl"
    else
        # Subsequent iterations: use domain from previous iteration with hypothesis removed
        PREV_ITER_PREFIX="$WORK_DIR/iter_$(printf '%02d' $((iteration - 1)))"
        CURRENT_DOMAIN="${PREV_ITER_PREFIX}_domain_reduced.hddl"
        
        if [ ! -f "$CURRENT_DOMAIN" ]; then
            echo "Error: Reduced domain from previous iteration not found"
            exit 1
        fi
        
        cp "$CURRENT_DOMAIN" "${ITER_PREFIX}_domain.hddl"
    fi
    
    echo ""
    echo "Step 1: Domain preparation"
    echo "  Using domain: ${ITER_PREFIX}_domain.hddl"
    
    # ========================================================================
    # STEP 2: Parse and ground with observation-enforcing problem
    # ========================================================================
    
    echo ""
    echo "Step 2: Parse and ground"
    
    ./pandaPIparser "${ITER_PREFIX}_domain.hddl" "${PROBLEM_MTLT}" \
        "${ITER_PREFIX}_parsed.htn" > "${ITER_PREFIX}_parse.log" 2>&1
    
    if [ $? -ne 0 ]; then
        echo "Parsing failed. See ${ITER_PREFIX}_parse.log"
        exit 1
    fi
    echo "Parsing successful"
    
    ./pandaPIgrounder -q "${ITER_PREFIX}_parsed.htn" "${ITER_PREFIX}_grounded.psas" \
        > "${ITER_PREFIX}_ground.log" 2>&1
    
    if [ $? -ne 0 ]; then
        echo "Grounding failed. See ${ITER_PREFIX}_ground.log"
        exit 1
    fi
    echo "Grounding successful"
    
    # ========================================================================
    # STEP 3: Create observation-enforcing PGR problem
    # ========================================================================
    
    echo ""
    echo "Step 3: Create observation-enforcing problem"
    
    # Generate PGR file with observations
    # pandaPIpgrRepairVerify creates output in the same directory as the observation file
    # Use pgrpo (fully observable) since we're working with observed action sequences
    "./htnPrefixEncoding" pgrpo "${ITER_PREFIX}_grounded.psas" \
        "$WORK_DIR/observations.txt" $NUM_OBS > "${ITER_PREFIX}_pgr_gen.log" 2>&1
    
    if [ $? -ne 0 ]; then
        echo "PGR generation failed. See ${ITER_PREFIX}_pgr_gen.log"
        exit 1
    fi
    
    # The PGR file is created in the same directory as observations.txt
    PGR_FILE="$WORK_DIR/observations.txt-$(printf '%03d' $NUM_OBS).pgr"
    
    if [ ! -f "$PGR_FILE" ]; then
        echo "PGR file not found: $PGR_FILE"
        exit 1
    fi
    
    mv "$PGR_FILE" "${ITER_PREFIX}_obs.pgr"
    echo "PGR file created: ${ITER_PREFIX}_obs.pgr"
    
    # ========================================================================
    # STEP 4: Solve observation-enforcing problem
    # ========================================================================
    
    echo ""
    echo "Step 4: Solve observation-enforcing problem"
    
    ./pplanner "${ITER_PREFIX}_obs.pgr" > "${ITER_PREFIX}_obs.pgr.log" 2>&1
    
    if [ $? -ne 0 ]; then
        echo "Planning failed. See ${ITER_PREFIX}_obs.pgr.log"
        exit 1
    fi
    echo "Planning successful"
    
    # ========================================================================
    # STEP 5: Extract selected hypothesis from log
    # ========================================================================
    
    echo ""
    echo "Step 5: Extract selected hypothesis"
    
    # Use C++ helper to extract hypothesis
    SELECTED_HYPOTHESIS=$(./posterior_helper extract "${ITER_PREFIX}_obs.pgr.log" 2>/dev/null)
    
    if [ -z "$SELECTED_HYPOTHESIS" ]; then
        echo "Could not extract hypothesis. Manual inspection required."
        echo "  Please check: ${ITER_PREFIX}_obs.pgr.log"
        exit 1
    fi
    
    echo "Selected hypothesis: $SELECTED_HYPOTHESIS"
    HYPOTHESES[$iteration]="$SELECTED_HYPOTHESIS"
    
    # ========================================================================
    # STEP 6: Create problem file with selected hypothesis as goal
    # ========================================================================
    
    echo ""
    echo "Step 6: Create baseline problem"
    
    # Extract instantiated subtasks directly from the observation-enforcing log
    SUBTASKS=$(./posterior_helper instantiated "${ITER_PREFIX}_obs.pgr.log" 2>/dev/null || true)

    if [ -z "$SUBTASKS" ]; then
        echo "  Warning: Could not extract instantiated subtasks from log"
        echo "  Falling back to using the selected hypothesis as the baseline goal: $SELECTED_HYPOTHESIS"
        # Create a baseline problem with the hypothesis as top-level goal
        ./posterior_helper problem "$WORK_DIR/problem_original.hddl" \
            "$SELECTED_HYPOTHESIS" "${ITER_PREFIX}_baseline_problem.hddl" > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            echo "Failed to create baseline problem using hypothesis as goal"
            exit 1
        fi
    else
        echo "  Subtasks: $SUBTASKS"
        # Use C++ helper to create problem file with subtasks
        ./posterior_helper problem "$WORK_DIR/problem_original.hddl" \
            "$SUBTASKS" "${ITER_PREFIX}_baseline_problem.hddl" > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            echo "Failed to create baseline problem from instantiated subtasks"
            exit 1
        fi
    fi
    
    if [ $? -ne 0 ]; then
        echo "Failed to create baseline problem"
        exit 1
    fi
    
    echo "Baseline problem created: ${ITER_PREFIX}_baseline_problem.hddl"
    
    # ========================================================================
    # STEP 7: Solve baseline (unconstrained) problem
    # ========================================================================
    
    echo ""
    echo "Step 7: Solve baseline problem"
    
    # Parse and ground baseline
    ./pandaPIparser "${ITER_PREFIX}_domain.hddl" "${ITER_PREFIX}_baseline_problem.hddl" \
        "${ITER_PREFIX}_baseline_parsed.htn" > "${ITER_PREFIX}_baseline_parse.log" 2>&1
    
    if [ $? -ne 0 ]; then
        echo "Baseline parsing failed"
        exit 1
    fi
    
    ./pandaPIgrounder -q "${ITER_PREFIX}_baseline_parsed.htn" \
        "${ITER_PREFIX}_baseline_grounded.psas" > "${ITER_PREFIX}_baseline_ground.log" 2>&1
    
    if [ $? -ne 0 ]; then
        echo "Baseline grounding failed"
        exit 1
    fi
    
    # Solve baseline (no observation enforcement, just pure HTN planning)
    ./pplanner "${ITER_PREFIX}_baseline_grounded.psas" > "${ITER_PREFIX}_baseline.log" 2>&1
    
    if [ $? -ne 0 ]; then
        echo "Baseline planning failed"
        # This might be acceptable if hypothesis is unsolvable
        echo "  Note: Hypothesis may be unsolvable - setting likelihood to 0"
        LIKELIHOODS[$iteration]="0.0"
        
        # Write to likelihoods file for C++ processing
        echo "$SELECTED_HYPOTHESIS 0.0" >> "$LIKELIHOODS_FILE"
        
        # Create reduced domain for next iteration
        echo "  Creating reduced domain..."
        ./posterior_helper remove "${ITER_PREFIX}_domain.hddl" \
            "$SELECTED_HYPOTHESIS" "${ITER_PREFIX}_domain_reduced.hddl" > /dev/null 2>&1
        continue
    fi
    
    echo "Baseline planning successful"
    
    # ========================================================================
    # STEP 8: Compute normalized likelihood
    # ========================================================================
    
    echo ""
    echo "Step 8: Compute normalized likelihood"
    echo "  Using numerator plan from Step 4: ${ITER_PREFIX}_obs.pgr.log"
    echo "  Using denominator plan from Step 7: ${ITER_PREFIX}_baseline.log"
    echo "  Using baseline grounded model: ${ITER_PREFIX}_baseline_grounded.psas"
    
    # The numerator plan (observation-consistent) was already computed in Step 4
    # when we solved the dummy top-level problem with observation enforcement.
    # The denominator plan (unconstrained baseline) was computed in Step 7.
    # IMPORTANT: We use the baseline grounded model (with just the selected hypothesis)
    # for Stage I probability calculation, NOT the dummy top-level grounded model.
    
    if [ -f "./compute_normalized_likelihood" ]; then
        ./compute_normalized_likelihood \
            "${ITER_PREFIX}_baseline_grounded.psas" \
            "${ITER_PREFIX}_obs.pgr.log" \
            "${ITER_PREFIX}_baseline.log" \
            1.0 $NUM_OBS 1 0.9 | tee "${ITER_PREFIX}_likelihood.txt"
        
        if [ $? -eq 0 ]; then
            # Extract likelihood value from output
            LIKELIHOOD=$(grep "Normalized Likelihood:" "${ITER_PREFIX}_likelihood.txt" -A 1 | \
                        grep "P̂" | \
                        sed 's/.*= //')
            
            if [ -z "$LIKELIHOOD" ]; then
                LIKELIHOOD="0.0"
            fi
            
            echo "Likelihood: $LIKELIHOOD"
            LIKELIHOODS[$iteration]="$LIKELIHOOD"
            
            # Write to likelihoods file for C++ processing
            echo "$SELECTED_HYPOTHESIS $LIKELIHOOD" >> "$LIKELIHOODS_FILE"
        else
            echo "Likelihood computation failed"
            LIKELIHOODS[$iteration]="0.0"
            
            # Write to likelihoods file for C++ processing
            echo "$SELECTED_HYPOTHESIS 0.0" >> "$LIKELIHOODS_FILE"
        fi
    else
        echo "  Note: compute_normalized_likelihood not found, skipping likelihood computation"
        LIKELIHOODS[$iteration]="N/A"
        
        # Write to likelihoods file for C++ processing
        echo "$SELECTED_HYPOTHESIS 0.0" >> "$LIKELIHOODS_FILE"
    fi
    
    # ========================================================================
    # STEP 9: Create reduced domain for next iteration
    # ========================================================================
    
    echo ""
    echo "Step 9: Prepare for next iteration"
    
    # Use C++ helper to remove hypothesis from domain
    ./posterior_helper remove "${ITER_PREFIX}_domain.hddl" \
        "$SELECTED_HYPOTHESIS" "${ITER_PREFIX}_domain_reduced.hddl" > /dev/null 2>&1
    
    if [ $? -ne 0 ]; then
        echo "Failed to create reduced domain"
        exit 1
    fi
    
    echo "Reduced domain created: ${ITER_PREFIX}_domain_reduced.hddl"
    
    echo ""
    echo "Iteration $iteration complete"
    echo "  Hypothesis: $SELECTED_HYPOTHESIS"
    echo "  Likelihood: ${LIKELIHOODS[$iteration]}"
done

# ============================================================================
# FINAL RESULTS: COMPUTE NORMALIZED POSTERIORS USING C++
# ============================================================================

echo ""
echo "============================================================"
echo "POSTERIOR ESTIMATION RESULTS"
echo "============================================================"
echo ""

# Check if compute_posterior tool is available
if [ ! -f "./compute_posterior" ]; then
    echo "Error: compute_posterior tool not found"
    echo "Please build it first: cd build && make compute_posterior"
    exit 1
fi

# Run C++ program to compute normalized posteriors
POSTERIORS_FILE="$WORK_DIR/posteriors.txt"
./compute_posterior "$LIKELIHOODS_FILE" "$POSTERIORS_FILE"

if [ $? -ne 0 ]; then
    echo "Error: Failed to compute posteriors"
    exit 1
fi

echo ""
echo "============================================================"
echo "Results by Iteration Order (Discovery Order)"
echo "============================================================"
echo ""

# Display results in iteration order (from likelihoods file)
iter=1
while IFS=' ' read -r hyp_name likelihood; do
    # Skip comment lines
    if [[ "$hyp_name" == "#" ]]; then
        continue
    fi
    
    echo "Iteration $iter: $hyp_name"
    echo "  Likelihood: $likelihood"
    echo ""
    
    iter=$((iter + 1))
done < "$LIKELIHOODS_FILE"

echo ""
echo "============================================================"
echo "Results Ranked by Posterior (Sorted by Probability)"
echo "============================================================"
echo ""

# Read and display sorted results from posteriors file
declare -a POSTERIORS
i=1
while IFS=' ' read -r hyp_name likelihood posterior; do
    # Skip comment lines
    if [[ "$hyp_name" == "#" ]]; then
        continue
    fi
    
    POSTERIORS[$i]="$posterior"
    
    echo "Rank $i: $hyp_name"
    echo "  Likelihood: $likelihood"
    echo "  Posterior:  $posterior"
    echo ""
    
    i=$((i + 1))
done < <(grep -v '^#' "$POSTERIORS_FILE")

# Save comprehensive results to file
RESULTS_FILE="$WORK_DIR/posterior_results.txt"
{
    echo "Posterior Estimation Results"
    echo "============================"
    echo ""
    echo "Configuration:"
    echo "  Domain:       $DOMAIN_FILE"
    echo "  Problem:      $PROBLEM_FILE"
    echo "  Observations: $NUM_OBS"
    echo "  K iterations: $K_ITERATIONS"
    echo ""
    echo "Results:"
    echo ""
    
    # Copy the detailed posteriors output
    cat "$POSTERIORS_FILE"
    
} > "$RESULTS_FILE"

echo ""
echo "Results saved to: $RESULTS_FILE"
echo ""
echo "All intermediate files saved in: $WORK_DIR"
echo ""
