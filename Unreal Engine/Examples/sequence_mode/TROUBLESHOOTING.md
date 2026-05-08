# PROXSIMA Sequence Mode Troubleshooting Guide

## Common Issues

### Configuration Problems

1. **Invalid Configuration Format**
   ```
   Error: Failed to parse configuration file
   ```
   - Verify JSON syntax is correct
   - Check that all required fields are present
   - Use a JSON validator to check format

2. **Missing Files**
   ```
   Error: Failed to load sequence file
   ```
   - Verify file paths are correct
   - Check that referenced files exist
   - Ensure paths are relative to configuration file

3. **Camera Not Found**
   ```
   Error: Failed to find camera actor for: [camera_name]
   ```
   - Verify camera names match in both config files
   - Check that sensors are properly initialized
   - Ensure camera actors are spawned

### Sequence Problems

1. **Invalid Camera Poses**
   ```
   Warning: Invalid quaternion rotation
   ```
   - Ensure quaternions are normalized
   - Check rotation values are valid
   - Verify coordinate system conventions

2. **Timing Issues**
   ```
   Warning: Timestamps not in ascending order
   ```
   - Sort camera poses by timestamp
   - Remove duplicate timestamps
   - Ensure time values are in seconds

### Runtime Issues

1. **Performance Problems**
   - Too many cameras active
   - High capture resolution
   - Frequent pose updates

   Solutions:
   - Reduce number of simultaneous cameras
   - Lower capture resolution
   - Increase time between poses

2. **Camera Jitter**
   - Not enough keyframes
   - Sharp rotations
   - Timestamp gaps too large

   Solutions:
   - Add intermediate poses
   - Smooth rotation changes
   - Use consistent time intervals

## Diagnostics

### Debug Output
Enable verbose logging in your configuration:
```json
{
    "debug": {
        "verboseLogging": true,
        "logCameraPoses": true
    }
}
```

### Verification Steps

1. Configuration Loading
   ```cpp
   // Check configuration loading
   bool success = GameInstance->InitializeSimulation(ConfigPath);
   if (!success) {
       // Handle error
   }
   ```

2. Camera Setup
   ```cpp
   // Verify camera initialization
   AActor* camera = GameInstance->GetSensorByName("main_camera");
   if (!camera) {
       // Camera not found
   }
   ```

3. Sequence Playback
   ```cpp
   // Monitor sequence updates
   void OnSequenceUpdate(float TimeStamp) {
       // Log camera transforms
   }
   ```

## Best Practices for Troubleshooting

1. **Incremental Testing**
   - Start with single camera
   - Add poses one at a time
   - Verify each step

2. **Validate Data**
   - Use valid quaternions
   - Check coordinate systems
   - Verify scale units

3. **Monitor Performance**
   - Watch frame rate
   - Check memory usage
   - Monitor file I/O

4. **Common Checks**
   - File permissions
   - Path validity
   - JSON syntax
   - Camera names
   - Transform values

## Getting Help

1. **Log Files**
   - Enable verbose logging
   - Check UE4 output log
   - Review camera position data

2. **Debug Tools**
   - Use UE4 debugger
   - Monitor transforms
   - Visualize camera paths

3. **Support Resources**
   - Documentation
   - Example configurations
   - Issue tracker