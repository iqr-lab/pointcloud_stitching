import argparse
import subprocess
from os.path import dirname, abspath

parser = argparse.ArgumentParser(
    description="Run any arbitrary script on the edge servers"
)
parser.add_argument(
    "script",
    metavar="SCRIPT",
    type=str,
    help="Path to the script to run on the edge servers. This path is relative to this host.py file",
)

parser.add_argument(
    "--hosts",
    dest="hosts",
    type=int,
    nargs="+",
    help="List of edge device numbers to run the script on. The corresponding hostnames must exist in the HOSTS file. If not provided, the script will run on all hosts in the HOSTS file.",
)

parser.add_argument(
    "--user",
    dest="user",
    type=str,
    default="lab",
    help="Username to use for SSH connections. Default is 'lab'.",
)

args = parser.parse_args()
file_parent_dir = dirname(abspath(__file__))

# Read the list of hosts from the HOSTS file
with open(f"{file_parent_dir}/HOSTS") as f:
   hosts = f.read().splitlines()
   
# Get subset of hosts if provided
if args.hosts:
   hosts = {host for host in hosts if any(f"{index}" in host for index in args.hosts)}

# Get the full path to the script
script_path = f"{file_parent_dir}/{args.script}"

# Run the script on the hosts
for host in hosts:
    print(f"\nRunning script on {host}\n{'=' * 80}")
    subprocess.run(f"cat {script_path} | ssh -q {args.user}@{host} /usr/bin/fish", shell=True, check=True)