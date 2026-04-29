#!/bin/bash
#SBATCH --job-name=cos568-async
#SBATCH --output=benchmark_%j.out
#SBATCH --error=benchmark_%j.err
#SBATCH --time=06:00:00
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=32G
#SBATCH --partition=cpu

cd /scratch/gpfs/MONA/Toki/Academic/COS568/COS568-LI-SP26
bash scripts/run_async.sh
