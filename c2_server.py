from flask import Flask, request, jsonify
import time

app = Flask(__name__)

# Simple command queue (in a real setup, this could be a database or more sophisticated logic)
commands = []

@app.route('/get_command', methods=['GET'])
def get_command():
    # If there are no commands, respond with an empty message
    if commands:
        command = commands.pop(0)  # Get the first command from the list
        return jsonify({'command': command}), 200
    return jsonify({'command': None}), 200

@app.route('/send_command', methods=['POST'])
def send_command():
    data = request.json
    if 'command' in data:
        command = data['command']
        commands.append(command)  # Store the new command
        return jsonify({'message': 'Command added successfully'}), 200
    return jsonify({'message': 'Invalid command'}), 400

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
