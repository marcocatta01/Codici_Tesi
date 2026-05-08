import websockets
import json
import time
import asyncio

async def send_capture_command(websocket, camera_name, position, rotation, command_id=None, timestamp=None):
    command = {
        "type": "capture_image",
        "camera": camera_name,
        "pose": {
            "location": {"x": position[0], "y": position[1], "z": position[2]},
            "rotation": {"x": rotation[0], "y": rotation[1], "z": rotation[2], "w": rotation[3]},
            "timestamp": timestamp or time.time(),
            "metadata": {"description": f"Capture from position {position}"}
        }
    }
    
    if command_id:
        command["commandId"] = command_id
    
    await websocket.send(json.dumps(command))
    print(f"Sent capture command for {camera_name}")

async def send_shutdown_command(websocket, command_id=None):
    command = {
        "type": "shutdown"
    }
    
    if command_id:
        command["commandId"] = command_id
    
    await websocket.send(json.dumps(command))
    print(f"Sent shutdown command")

async def handle_messages(websocket):
    """Handle incoming messages from PROX-SIMA"""
    async for message in websocket:
        try:
            data = json.loads(message)
            if data.get('type') == 'capture_command_ack':
                command_id = data.get('commandId', 'N/A')
                camera_name = data.get('cameraName', 'Unknown')
                success = data.get('success', False)
                
                if success:
                    print(f"✓ Capture {command_id} ({camera_name}): Success")
                else:
                    error = data.get('error', 'Unknown error')
                    print(f"✗ Capture {command_id} ({camera_name}): Failed - {error}")
            elif data.get('type') == 'shutdown_command_ack':
                command_id = data.get('commandId', 'N/A')
                success = data.get('success', False)
                
                if success:
                    print(f"✓ Shutdown {command_id}: Success - UE4 will close shortly")
                else:
                    error = data.get('error', 'Unknown error')
                    print(f"✗ Shutdown {command_id}: Failed - {error}")
        except json.JSONDecodeError:
            print(f"Received non-JSON message: {message}")

async def main():
    uri = "ws://localhost:8080/control"
    
    try:
        async with websockets.connect(uri) as websocket:
            print(f"Connected to PROX-SIMA at {uri}")
            
            # Start message handler in background
            message_task = asyncio.create_task(handle_messages(websocket))
            
            # Wait a moment for connection to stabilize
            await asyncio.sleep(1)
            
            # Send capture commands
            await send_capture_command(websocket, "main_camera", [0, 10, 1], [0, 0, -0.707, 0.707], "cmd_001", 0.123)
            await asyncio.sleep(0.5)  # Small delay between commands

            await send_capture_command(websocket, "main_camera", [1, 0, 10], [-0.408, 0.408, 0.577, 0.577], "cmd_002", 1.31)
            await asyncio.sleep(0.5)

            await send_capture_command(websocket, "secondary_camera", [10, 20, 0], [-0.183, 0.183, -0.683, 0.683], "cmd_003", 2.00)
            await asyncio.sleep(0.5)

            # Send shutdown command after all captures are done
            await send_shutdown_command(websocket, "shutdown_001")
            
            # Wait for acknowledgments
            await asyncio.sleep(5)
            
            # Cancel message handler
            message_task.cancel()
            
    except websockets.exceptions.ConnectionClosed:
        print("Connection to PROX-SIMA closed")
    except Exception as e:
        print(f"Error connecting to PROX-SIMA: {e}")

if __name__ == "__main__":
    asyncio.run(main())
