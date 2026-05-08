# PROXSIMA Sequence Mode Documentation

## Overview
The sequence mode allows for pre-defined camera animation playback without requiring an FMU simulation. This mode is useful for:
- Creating scripted camera movements
- Capturing scenes from multiple viewpoints
- Generating training data for computer vision
- Recording cinematic sequences

## Configuration Files

### sequence_config.json
The main configuration file that defines:
- Simulation mode ("sequence")
- Camera sequence file location
- Sensor configurations (cameras, etc.)
- Output settings

Example:
```json
{
    "simulationMode": "sequence",
    "sequence": {
        "sequenceFile": "camera_poses.json",
        "outputPath": "output/sequence_captures"
    }
}
```

### camera_poses.json
Defines the camera animation sequences including:
- Camera poses over time
- Multiple camera support
- Interpolation between keyframes
- Metadata for each pose

Example:
```json
{
    "sequences": [
        {
            "camera": "main_camera",
            "poses": [
                {
                    "timestamp": "0.0",
                    "location": {"x": 0, "y": 0, "z": 1},
                    "rotation": {"x": 0, "y": 0, "z": 0, "w": 1}
                }
            ]
        }
    ]
}
```

## Usage

1. Create configuration files:
   - `sequence_config.json`: Main configuration
   - `camera_poses.json`: Camera animation data

2. Place files in your project directory

3. Load in PROXSIMA:
   ```cpp
   GameInstance->InitializeSimulation("path/to/sequence_config.json");
   ```

## Camera Pose Format

### Location
- Uses Unreal Engine coordinates (centimeters)
- Right-handed coordinate system
- X: Forward
- Y: Right
- Z: Up

### Rotation
- Quaternion format (x, y, z, w)
- Right-handed coordinate system
- Automatically converted to Unreal Engine's left-handed system

### Timestamps
- In seconds
- Must be in ascending order
- Linear interpolation between keyframes

## Best Practices

1. Camera Movement
   - Keep movements smooth
   - Use enough keyframes for complex paths
   - Consider camera acceleration/deceleration

2. Performance
   - Limit number of simultaneous cameras
   - Use appropriate capture rates
   - Consider output resolution impact

3. Data Organization
   - Use descriptive camera names
   - Add metadata for pose identification
   - Keep sequences modular

## Error Handling

Common issues and solutions:
- Missing files: Check path to config files
- Invalid poses: Verify quaternion normalization
- Timing issues: Ensure timestamps are ascending
- Camera not found: Check camera names match in both configs