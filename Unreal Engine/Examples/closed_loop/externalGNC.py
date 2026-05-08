#!/usr/bin/env python3

import asyncio
import os
import websockets
import json
import time
import numpy as np
import pandas as pd
import io
from PIL import Image
import matplotlib
matplotlib.use('TkAgg')  # Use TkAgg backend for better Windows compatibility
import matplotlib.pyplot as plt
import queue

class SpacecraftGNC:
    def __init__(self, base_uri="ws://localhost:8080"):
        self.base_uri = base_uri
        self.camera_websockets = {}    # For receiving images
        self.control_websocket = None  # For sending commands
        self.control_active = False
        self.simulation_started = False  # Detect based on image flow
        self.last_image_time = {}  # Track last image time per camera
        self.last_control_time = 0.0
        self.control_horizon = 10.0  # seconds to send ahead
        self.control_processing = False  # Flag to prevent concurrent control processing

        # Camera endpoints - direct connection to image streams
        self.camera_endpoints = {
            "MainCamera": f"{base_uri}/MainCamera",
            "EECamera": f"{base_uri}/EECamera"
        }

        # Control endpoint for sending commands
        self.control_endpoint = f"{base_uri}/control"

        # Load CSV data for command generation
        self.csv_data = self.load_csv_data()

        # Image display setup
        self.fig = None
        self.ax1 = None
        self.ax2 = None
        self.image_display_main = None
        self.image_display_ee = None
        self.image_queue = queue.Queue()
        self.display_initialized = False

    def load_csv_data(self):
        """Load the CSV file with control commands"""
        try:
            df = pd.read_csv(os.path.dirname(os.path.realpath(__file__)) + '/fmu_inputs.csv')
            print(f"Loaded CSV data with {len(df)} time points")
            return df
        except Exception as e:
            print(f"Error loading CSV data: {e}")
            return None

    def setup_display(self):
        """Setup the matplotlib display in the main thread"""
        if not self.display_initialized:
            plt.ion()
            self.fig, (self.ax1, self.ax2) = plt.subplots(1, 2, figsize=(15, 6))
            self.ax1.set_title("Main Camera - Waiting for images...")
            self.ax2.set_title("End Effector Camera - Waiting for images...")
            self.ax1.axis('off')
            self.ax2.axis('off')
            plt.tight_layout()
            plt.show()
            self.display_initialized = True

    def update_display(self):
        """Update the display with any queued images"""
        try:
            while not self.image_queue.empty():
                image_data = self.image_queue.get_nowait()
                self.display_image(image_data['image'], image_data['camera_name'])
        except queue.Empty:
            pass
        except Exception as e:
            print(f"Error updating display: {e}")

    async def connect_to_cameras(self):
        """Test camera endpoint availability"""
        print("🔍 Testing camera endpoint availability...")

        success_count = 0
        for camera_name, endpoint in self.camera_endpoints.items():
            try:
                print(f"🔗 Testing connection to {camera_name} at {endpoint}...")
                async with websockets.connect(endpoint):
                    print(f"✅ {camera_name} endpoint is available")
                    success_count += 1
            except Exception as e:
                print(f"❌ Failed to connect to {camera_name}: {e}")

        if success_count > 0:
            print(f"🚀 {success_count}/{len(self.camera_endpoints)} camera endpoints are available")
            return True
        else:
            print("❌ No camera endpoints available")
            return False


    async def handle_image_with_timestamp(self, camera_name, binary_data, simulation_time):
        """Handle incoming PNG image data with simulation timestamp"""
        try:
            # Mark simulation as started on first image
            if not self.simulation_started:
                print(f"🕐 Simulation detected - using simulation timestamps from {camera_name}")
                self.simulation_started = True

            self.last_image_time[camera_name] = time.time()

            # Convert binary PNG data directly to PIL Image
            image = Image.open(io.BytesIO(binary_data))
            image_array = np.array(image)

            # Queue image for display
            self.image_queue.put({
                'image': image_array,
                'camera_name': camera_name
            })

            # Trigger GNC computation with accurate simulation time
            await self.compute_and_send_controls(simulation_time)

        except Exception as e:
            print(f"❌ Error processing timestamped image from {camera_name}: {e}")
            print(f"Simulation time: {simulation_time}, Data length: {len(binary_data)}")

    def display_image(self, image, camera_name):
        """Display the received image in matplotlib window"""
        try:
            if not self.display_initialized:
                return

            if camera_name == "MainCamera":
                if self.image_display_main is None:
                    self.image_display_main = self.ax1.imshow(image)
                    self.ax1.set_title(f"Main Camera (Last: {time.strftime('%H:%M:%S')})")
                else:
                    self.image_display_main.set_array(image)
                    self.ax1.set_title(f"Main Camera (Last: {time.strftime('%H:%M:%S')})")
            elif camera_name == "EECamera":
                if self.image_display_ee is None:
                    self.image_display_ee = self.ax2.imshow(image)
                    self.ax2.set_title(f"End Effector Camera (Last: {time.strftime('%H:%M:%S')})")
                else:
                    self.image_display_ee.set_array(image)
                    self.ax2.set_title(f"End Effector Camera (Last: {time.strftime('%H:%M:%S')})")

            # Refresh the display
            self.fig.canvas.draw_idle()
            self.fig.canvas.flush_events()

        except Exception as e:
            print(f"Error displaying image: {e}")

    async def setup_control_connection(self):
        """Setup persistent connection for sending control commands"""
        try:
            print(f"🔗 Establishing control connection to {self.control_endpoint}")
            self.control_websocket = await websockets.connect(self.control_endpoint)

            # Send initial connection confirmation
            confirmation = {
                "type": "control_client_ready",
                "timestamp": time.time()
            }
            await self.control_websocket.send(json.dumps(confirmation))
            print("✅ Control connection established and registered")
            
            # OPTIONAL: Send initial control data for t=0 to t=10s to eliminate startup delay
            print("📦 Sending initial control batch for t=0.0 to t=10.0s...")
            await self.send_initial_control_batch()
            
            return True
        except Exception as e:
            print(f"❌ Failed to establish control connection: {e}")
            import traceback
            traceback.print_exc()
            return False

    async def send_initial_control_batch(self):
        """Send initial control data for the first 10 seconds to eliminate startup delays"""
        if self.csv_data is None:
            print("⚠️  No CSV data available for initial control batch")
            return
            
        try:
            # Check if CSV data starts from t≈0.0 (within 0.1s tolerance)
            min_time = self.csv_data['time'].min()
            if min_time > 0.1:
                print(f"ℹ️  CSV data starts at t={min_time:.3f}s (not t≈0.0), skipping initial control batch")
                print("   Initial control batch is only sent when CSV data starts from simulation start")
                return
            
            # Get initial control data for t=0 to t=10s
            initial_data = self.csv_data[
                (self.csv_data['time'] >= 0.0) &
                (self.csv_data['time'] <= 10.0)
            ]
            
            if len(initial_data) == 0:
                print("⚠️  No initial control data found for t=0 to t=10s")
                return
                
            print(f"📡 CSV data starts at t={min_time:.3f}s - sending initial control batch with {len(initial_data)} time points")
            await self.send_control_commands(initial_data, 0.0)
            print("✅ Initial control batch sent successfully")
            
        except Exception as e:
            print(f"❌ Failed to send initial control batch: {e}")
            import traceback
            traceback.print_exc()

    async def send_control_commands(self, control_data, current_sim_time):
        """Send control commands to the simulation via persistent WebSocket connection"""
        if not self.control_websocket:
            print("⚠️  No control connection available, skipping command send")
            return

        try:
            # [DEBUG] Start timing control message construction
            construction_start_time = time.time()

            # Get all control variable names from the CSV data
            control_variables = [col for col in control_data.columns if col != 'time']

            # Build a single batch command containing all variables for atomic update
            batch_command = {
                "type": "load_timeseries_batch",
                "variables": []
            }

            for var in control_variables:
                if var in control_data.columns:
                    # Build time-value pairs for this variable
                    time_values = []
                    for _, row in control_data.iterrows():
                        time_values.append({
                            "time": float(row['time']),
                            "value": float(row[var])
                        })
                    
                    # Debug log for critical variables
                    if var in ['T[1]', 'q[1]'] and len(time_values) > 0:
                        print(f"[DEBUG] Batching {var}: first value {time_values[0]['value']:.6f} at time {time_values[0]['time']:.3f}, last value {time_values[-1]['value']:.6f} at time {time_values[-1]['time']:.3f}")

                    # Add to batch
                    batch_command["variables"].append({
                        "sourceId": f"gnc_{var}",
                        "timeValues": time_values
                    })

            # [DEBUG] Complete timing control message construction
            construction_end_time = time.time()
            construction_duration = construction_end_time - construction_start_time
            
            # [DEBUG] Start timing WebSocket send
            websocket_send_start_time = time.time()
            
            # Send single atomic batch command for all variables  
            await self.control_websocket.send(json.dumps(batch_command))
            
            # [DEBUG] Complete timing WebSocket send
            websocket_send_end_time = time.time()
            websocket_send_duration = websocket_send_end_time - websocket_send_start_time
            
            commands_sent = len(control_variables)
            total_time_points = sum(len(var_data["timeValues"]) for var_data in batch_command["variables"])
            message_size = len(json.dumps(batch_command))

            # Realistic GNC logging
            if self.last_control_time == 0.0:
                print(f"🚀 Initial control commands sent: {commands_sent} variables for time {current_sim_time:.3f}s to {current_sim_time + self.control_horizon:.3f}s")
                print(f"   [TIMING] Construction: {construction_duration*1000:.2f}ms, WebSocket send: {websocket_send_duration*1000:.2f}ms")
                print(f"   [SIZE] Message: {message_size/1024:.1f}KB, Time points: {total_time_points}")
            else:
                print(f"🎯 Trajectory correction: {commands_sent} variables updated from {current_sim_time:.3f}s to {current_sim_time + self.control_horizon:.3f}s")
                print(f"   [TIMING] Construction: {construction_duration*1000:.2f}ms, WebSocket send: {websocket_send_duration*1000:.2f}ms")

        except Exception as e:
            print(f"❌ Error sending control commands: {e}")
            import traceback
            traceback.print_exc()
            # Reset connection on error to trigger reconnection
            self.control_websocket = None
            
            # Immediately retry the connection and command sending
            print("🔄 Retrying control command transmission...")
            if await self.setup_control_connection():
                try:
                    # Re-attempt sending commands with the new connection
                    await self.send_control_commands(control_data, current_sim_time)
                except Exception as retry_error:
                    print(f"❌ Retry also failed: {retry_error}")

    async def compute_and_send_controls(self, current_sim_time):
        """Compute control commands based on simulation time"""
        if not self.simulation_started or self.csv_data is None:
            return

        # Prevent concurrent control processing from multiple camera streams
        # Both cameras send identical simulation timestamps, so we need this flag to prevent duplicates
        if self.control_processing:
            print(f"[DEBUG] Skipping control processing for t={current_sim_time:.3f}s - already processing")
            return

        # Send immediately on first image, then every 5 seconds for trajectory corrections
        time_diff = current_sim_time - self.last_control_time
        if self.last_control_time > 0.0 and time_diff < 5.0:
            print(f"[DEBUG] Skipping control processing for t={current_sim_time:.3f}s - within interval (last: {self.last_control_time:.3f}s, diff: {time_diff:.3f}s)")
            return
        
        print(f"[DEBUG] Starting control processing for t={current_sim_time:.3f}s (last: {self.last_control_time:.3f}s)")
        
        # [DEBUG] Start timing overall control processing
        control_processing_start_time = time.time()

        # Set processing flag and update control time to prevent concurrent execution
        self.control_processing = True
        self.last_control_time = current_sim_time

        # Control connection should already be established - if not, something is wrong
        if not self.control_websocket:
            print("❌ Control connection lost! Cannot send control commands")
            self.control_processing = False  # Clear flag before returning
            return

        try:
            # Send timeseries data for the next control horizon
            future_time = current_sim_time + self.control_horizon

            # Find the data rows that cover the next control horizon with optimization
            # Add a small buffer for interpolation but limit data points for efficiency
            time_buffer = 0.1  # 100ms buffer for interpolation
            start_time = max(0.0, current_sim_time - time_buffer)
            
            future_data = self.csv_data[
                (self.csv_data['time'] >= start_time) &
                (self.csv_data['time'] <= future_time)
            ]

            if len(future_data) == 0:
                print(f"No control data available for time range {current_sim_time:.3f} to {future_time:.3f}")
                return

            # [OPTIMIZATION] Limit data points to reduce message size if too many points
            max_data_points = 100  # Reasonable limit for 10s horizon with 0.1s intervals
            if len(future_data) > max_data_points:
                # Sample data points evenly across the time range
                step = len(future_data) // max_data_points
                future_data = future_data.iloc[::step]
                print(f"[OPTIMIZATION] Reduced data points from {len(self.csv_data)} to {len(future_data)} for efficiency")

            print(f"🚀 Computing GNC controls for next {self.control_horizon}s ({len(future_data)} data points)")

            # Note: This is a simplified demo - in a real system, you would:
            # 1. Process the received images with computer vision
            # 2. Run guidance/navigation algorithms
            # 3. Compute control commands based on the state estimate
            #
            # For this demo, we use the pre-loaded CSV data as control commands
            # and send them to the simulation via the /unreal WebSocket endpoint

            # Send the control commands to the simulation
            await self.send_control_commands(future_data, current_sim_time)

            # [DEBUG] Complete timing overall control processing
            control_processing_end_time = time.time()
            control_processing_duration = control_processing_end_time - control_processing_start_time

            print(f"✓ GNC computation complete for t={current_sim_time:.3f}s")
            print(f"   → Sent timeseries data for next {self.control_horizon}s")
            print(f"   → Control variables: {[col for col in self.csv_data.columns if col != 'time']}")
            print(f"   [TIMING] Total control processing: {control_processing_duration*1000:.2f}ms")

        except Exception as e:
            print(f"Error computing controls: {e}")
            import traceback
            traceback.print_exc()
        finally:
            # Clear processing flag to allow future control processing
            self.control_processing = False

    def check_simulation_health(self):
        """Check if we're still receiving images (simulation health)"""
        current_time = time.time()

        if not self.last_image_time:
            return True  # No images received yet

        # Check if any camera has sent images recently
        for camera_name, last_time in self.last_image_time.items():
            time_since_last = current_time - last_time
            if time_since_last > 10.0:  # No images for 10 seconds
                print(f"⚠️  No images from {camera_name} for {time_since_last:.1f}s")
            elif time_since_last > 30.0:  # No images for 30 seconds
                print(f"❌ {camera_name} appears to have stopped - consider reconnecting")
                return False

        return True

    async def camera_listener_task(self, camera_name, endpoint):
        """Individual camera listener task - matches working test pattern"""
        print(f"🎧 Starting {camera_name} listener task")

        while self.control_active:
            try:
                print(f"🔗 {camera_name}: Connecting to {endpoint}...")
                async with websockets.connect(endpoint) as websocket:
                    print(f"✅ {camera_name}: Connected and listening for data")

                    message_count = 0
                    try:
                        async for message in websocket:
                            if not self.control_active:
                                break

                            message_count += 1

                            if isinstance(message, str):
                                # JSON format with timestamp (only supported format)
                                try:
                                    data = json.loads(message)
                                    if data.get('type') == 'data' and 'data' in data and 'timestamp' in data:
                                        # Decode base64 image data
                                        import base64
                                        binary_data = base64.b64decode(data['data'])
                                        if binary_data.startswith(b'\x89PNG\r\n\x1a\n'):
                                            simulation_time = data['timestamp']
                                            await self.handle_image_with_timestamp(camera_name, binary_data, simulation_time)
                                        else:
                                            print(f"⚠️  {camera_name}: Received non-PNG data in JSON message")
                                    else:
                                        print(f"⚠️  {camera_name}: Received unexpected JSON message: {message[:100]}...")
                                except json.JSONDecodeError:
                                    print(f"⚠️  {camera_name}: Received invalid JSON: {message[:100]}...")
                            else:
                                print(f"⚠️  {camera_name}: Only JSON format with timestamps is supported")

                    except websockets.exceptions.ConnectionClosed as e:
                        print(f"📡 {camera_name}: Connection closed normally - Code: {e.code}, Reason: '{e.reason}'")
                        # Don't immediately reconnect on normal close, check if control is still active
                        if not self.control_active:
                            break

            except websockets.exceptions.ConnectionClosed as e:
                print(f"📡 {camera_name}: Connection closed during setup - Code: {e.code}, Reason: '{e.reason}'")
            except Exception as e:
                print(f"❌ {camera_name}: Error - {e}")
                import traceback
                traceback.print_exc()

            if self.control_active:
                print(f"🔄 {camera_name}: Reconnecting in 2 seconds...")
                await asyncio.sleep(2)
            else:
                break

        print(f"🏁 {camera_name}: Listener task finished")

    async def guidance_navigation_control_loop(self):
        """Main GNC loop"""
        self.control_active = True

        # CRITICAL FIX: Establish control connection BEFORE starting camera listeners
        # This eliminates the random delay when first image arrives
        print("🔗 Pre-establishing control connection before simulation starts...")
        if not await self.setup_control_connection():
            print("❌ Failed to establish control connection - GNC loop cannot start")
            return
        
        print("✅ Control connection ready - starting camera listeners")

        # Start camera listener tasks using the working pattern
        camera_tasks = []
        for camera_name, endpoint in self.camera_endpoints.items():
            task = asyncio.create_task(self.camera_listener_task(camera_name, endpoint))
            camera_tasks.append(task)

        try:
            print("🎯 GNC loop started - monitoring cameras and processing images")

            # Main control loop
            while self.control_active:
                await asyncio.sleep(0.1)  # Short sleep for responsiveness

                # Update display
                if self.display_initialized:
                    self.update_display()
                    plt.pause(0.001)  # Allow matplotlib to process events

        except KeyboardInterrupt:
            print("🛑 Shutting down GNC loop...")
        finally:
            self.control_active = False
            # Cancel camera tasks
            for task in camera_tasks:
                if not task.done():
                    task.cancel()
            # Wait for tasks to finish
            if camera_tasks:
                await asyncio.gather(*camera_tasks, return_exceptions=True)

    async def shutdown(self):
        """Shutdown the GNC module"""
        self.control_active = False

        # Close control websocket connection
        if self.control_websocket:
            try:
                await self.control_websocket.close()
                print("🔌 Closed control connection")
            except Exception:
                pass

        # Close camera websocket connections
        for camera_name, websocket in self.camera_websockets.items():
            try:
                await websocket.close()
                print(f"🔌 Closed connection to {camera_name}")
            except Exception:
                pass

        if self.display_initialized:
            plt.close('all')

# Main execution
async def main():
    print("🚀 Starting External GNC Module")
    print("=" * 50)

    gnc = SpacecraftGNC()

    # Setup display in main thread
    gnc.setup_display()

    # Connect to camera endpoints
    if await gnc.connect_to_cameras():
        print("📡 Connected to cameras - waiting for image data...")
        print("📊 Images will be displayed in matplotlib window")
        print("🎮 GNC will trigger on image receipt")
        print("🎯 Start the simulation in Unreal Engine to begin receiving images")
        print("Press Ctrl+C to exit")
        print("=" * 50)

        try:
            await gnc.guidance_navigation_control_loop()
        except KeyboardInterrupt:
            print("\n🛑 Shutting down...")
        finally:
            await gnc.shutdown()
    else:
        print("❌ Failed to connect to cameras. Ensure the simulation is running with WebSocket enabled.")

if __name__ == "__main__":
    # Install required packages if not present
    try:
        import websockets
        import matplotlib
    except ImportError as e:
        print(f"Missing required package: {e}")
        print("Install with: pip install websockets pandas matplotlib pillow opencv-python")
        exit(1)

    asyncio.run(main())
