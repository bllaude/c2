import requests
import time
import os
import subprocess

SERVER_URL = 'http://localhost:5000/get_command'

def execute_command(command):
    try:
        result = subprocess.run(command, shell=True, capture_output=True, text=True)
        return result.stdout if result.stdout else result.stderr
    except Exception as e:
        return str(e)

def poll_for_commands():
    while True:
        response = requests.get(SERVER_URL)
        if response.status_code == 200:
            data = response.json()
            command = data.get('command')
            if command:
                print(f"Received command: {command}")
                result = execute_command(command)
                print(f"Execution result: {result}")
            else:
                print("No commands to execute.")
        else:
            print("Error fetching command.")
        
        time.sleep(5)  # Poll every 5 seconds

if __name__ == '__main__':
    poll_for_commands()
