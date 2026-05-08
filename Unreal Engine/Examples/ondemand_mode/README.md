# OnDemand Mode Example

This example demonstrates PROX-SIMA's OnDemand simulation mode, which provides real-time image capture capabilities controlled via WebSocket commands.

## Overview

OnDemand mode allows you to:
- Keep the simulation running continuously 
- Send capture commands via WebSocket as needed
- Capture images from any configured camera at specific poses
- Receive acknowledgments for each capture operation

This mode is ideal for:
- Interactive image capture workflows
- External application control
- Real-time visualization tasks
- Custom automation scripts

## Configuration

The `ondemand_config.json` file contains:
- **simulationMode**: Set to "ondemand"
- **streamingPort**: WebSocket port for command communication (required)
- **sensors**: Array of camera sensors with their properties
- **ondemand**: Optional section for future ondemand-specific settings

### Key Configuration Requirements

1. **streamingPort**: Must be specified for WebSocket communication
2. **Camera sensors**: At least one camera sensor should be configured
3. **outputPath**: Configure where captured images will be saved

## Usage

### 1. Start the Simulation
Load the `ondemand_config.json` configuration file in PROX-SIMA. The simulation will:
- Initialize the camera sensors
- Start the WebSocket server on the specified port
- Wait for capture commands

### 2. Send Capture Commands via WebSocket

Connect to the WebSocket endpoint: `ws://localhost:8080/control`

#### Capture Command Format

```json
{
    "type": "capture_image",
    "commandId": "unique_command_id_123",
    "camera": "main_camera",
    "pose": {
        "location": {
            "x": 1.0,
            "y": 2.0,
            "z": 3.0
        },
        "rotation": {
            "x": 0.0,
            "y": 0.0,
            "z": 0.7071,
            "w": 0.7071
        },
        "timestamp": 1234567890.5,
        "metadata": {
            "description": "Custom capture from external application",
            "frame_id": "42",
            "custom_field": "value"
        }
    }
}
```

#### Command Fields

- **type**: Must be "capture_image"
- **commandId**: Unique identifier for tracking this command (optional)
- **camera**: Name of the camera sensor to use (must match configuration)
- **pose**: Camera pose data
  - **location**: Position in meters (x, y, z)
  - **rotation**: Orientation as quaternion (x, y, z, w)
  - **timestamp**: Timestamp in seconds (optional)
  - **metadata**: Additional key-value pairs (optional)

### 3. Receive Acknowledgments

After processing each capture command, PROX-SIMA sends an acknowledgment:

```json
{
    "type": "capture_command_ack",
    "commandId": "unique_command_id_123",
    "cameraName": "main_camera",
    "success": true,
    "timestamp": 1234567890.6
}
```

For failed captures:
```json
{
    "type": "capture_command_ack",
    "commandId": "unique_command_id_123", 
    "cameraName": "main_camera",
    "success": false,
    "error": "Camera sensor 'unknown_camera' not found",
    "timestamp": 1234567890.6
}
```

## Example Usage with Python

Install the required package:
```bash
pip install websockets
```

```python
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

            await send_capture_command(websocket, "main_camera", [5, 0, 5], [0, 0, 0.7071, 0.7071], "cmd_002")
            await asyncio.sleep(0.5)

            await send_capture_command(websocket, "secondary_camera", [0, 5, 3], [0, 0, 0, 1], "cmd_003")
            
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
```
## Coordinate System

- **Location**: Meters, right-handed coordinate system
- **Rotation**: Quaternion (x, y, z, w) representing orientation
- PROX-SIMA automatically converts to Unreal Engine's coordinate system (centimeters, left-handed)

## Output

Captured images are saved to the path specified in each camera's `outputPath` parameter. Images are typically saved as PNG files with timestamps in the filename.

## Tips

1. **Camera Names**: Ensure camera names in commands match those in the configuration
2. **WebSocket Connection**: Wait for the simulation to fully initialize before sending commands  
3. **Command IDs**: Use unique command IDs to track specific capture operations
4. **Error Handling**: Always check acknowledgment messages for success/failure status
5. **Performance**: OnDemand mode can handle multiple capture commands, but avoid overwhelming the system

## Troubleshooting

- **Connection Issues**: Verify the WebSocket port matches the configuration
- **Camera Not Found**: Check that camera names match the configuration exactly
- **Capture Failures**: Ensure the camera sensor is properly initialized and enabled
- **No Response**: Check that the capture command handler is registered (automatic in OnDemand mode)