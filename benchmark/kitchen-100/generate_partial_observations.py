#!/usr/bin/env python3
"""
Generate partial observations by randomly removing 20% of actions from solution files.
This simulates partial observability in plan/goal recognition scenarios.
"""

import os
import random
import sys
from pathlib import Path

def read_plan(filepath):
    """Read actions from a plan file, skipping empty lines and comments."""
    actions = []
    with open(filepath, 'r') as f:
        content = f.read().strip()
        
        # Parse actions in format (action args)(action args)...
        if content:
            # Split on ')(' to separate actions
            parts = content.replace(')(', ')||(').split('||')
            for part in parts:
                part = part.strip()
                if part and not part.startswith(';') and not part.startswith('#'):
                    # Ensure action has proper parentheses
                    if not part.startswith('('):
                        part = '(' + part
                    if not part.endswith(')'):
                        part = part + ')'
                    actions.append(part)
    return actions

def generate_partial_observation(actions, removal_rate=0.2):
    """
    Randomly remove a percentage of actions from the plan.
    
    Args:
        actions: List of action strings
        removal_rate: Fraction of actions to remove (default 0.2 = 20%)
    
    Returns:
        List of remaining actions in original order
    """
    n = len(actions)
    num_to_remove = int(n * removal_rate)
    
    # Create indices and randomly select which to remove
    indices = list(range(n))
    indices_to_remove = set(random.sample(indices, num_to_remove))
    
    # Keep only actions not selected for removal
    partial_observation = [action for i, action in enumerate(actions) 
                          if i not in indices_to_remove]
    
    return partial_observation

def write_plan(filepath, actions):
    """Write actions to a file, one per line."""
    with open(filepath, 'w') as f:
        for action in actions:
            f.write(action + '\n')

def main():
    # Set random seed for reproducibility
    random.seed(42)
    
    # Define directories
    script_dir = Path(__file__).parent
    solutions_dir = script_dir / '02-solutions'
    partial_dir = script_dir / '03-partial-solutions'
    
    # Check if solutions directory exists
    if not solutions_dir.exists():
        print(f"Error: Solutions directory not found: {solutions_dir}")
        sys.exit(1)
    
    # Create partial solutions directory if it doesn't exist
    partial_dir.mkdir(exist_ok=True)
    
    # Process each solution file
    solution_files = sorted(solutions_dir.glob('*.txt'))
    
    if not solution_files:
        print(f"No solution files found in {solutions_dir}")
        sys.exit(1)
    
    print(f"Processing {len(solution_files)} solution files...")
    print(f"Removing 20% of actions from each plan")
    print(f"Output directory: {partial_dir}")
    print()
    
    for solution_file in solution_files:
        # Read original plan
        actions = read_plan(solution_file)
        original_count = len(actions)
        
        if original_count == 0:
            print(f"Warning: {solution_file.name} has no actions, skipping...")
            continue
        
        # Generate partial observation
        partial_actions = generate_partial_observation(actions, removal_rate=0.2)
        partial_count = len(partial_actions)
        removed_count = original_count - partial_count
        
        # Write to output file
        output_file = partial_dir / solution_file.name
        write_plan(output_file, partial_actions)
        
        print(f"{solution_file.name}: {original_count} actions -> {partial_count} actions (removed {removed_count})")
    
    print()
    print(f"Done! Generated {len(solution_files)} partial observation files.")

if __name__ == '__main__':
    main()
