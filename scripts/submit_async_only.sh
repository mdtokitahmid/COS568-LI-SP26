#!/bin/bash
#SBATCH --job-name=cos568-fb-async-only
#SBATCH --output=benchmark_%j.out
#SBATCH --error=benchmark_%j.err
#SBATCH --time=04:00:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=32G
#SBATCH --partition=cpu

cd /home/mt3204/Academic/COS568/Project/COS568-LI-SP26
bash scripts/run_async_only.sh
