model Rover

  record RotationR
    Real q[4];
  end RotationR;

  record FramePose
    Real r_0[3](each unit="m");
    RotationR R;annotation(
      Placement(transformation(origin = {112, 50}, extent = {{-20, -20}, {20, 20}}), iconTransformation(origin = {102, 50}, extent = {{-10, -10}, {10, 10}})));
  end FramePose;
  
  output FramePose rover;
  output FramePose camera;

  Modelica.Blocks.Interfaces.RealInput q_rov[4] annotation(
    Placement(transformation(origin = {-106, 20}, extent = {{-20, -20}, {20, 20}}), iconTransformation(origin = {-86, 72}, extent = {{-20, -20}, {20, 20}})));
  Modelica.Blocks.Interfaces.RealInput q_cam[4] annotation(
    Placement(transformation(origin = {-106, -70}, extent = {{-20, -20}, {20, 20}}), iconTransformation(origin = {-84, 20}, extent = {{-20, -20}, {20, 20}})));
  Modelica.Blocks.Interfaces.RealInput r_rov[3] annotation(
    Placement(transformation(origin = {-106, 70}, extent = {{-20, -20}, {20, 20}}), iconTransformation(origin = {-74, -24}, extent = {{-20, -20}, {20, 20}})));
  Modelica.Blocks.Interfaces.RealInput r_cam[3] annotation(
    Placement(transformation(origin = {-106, -20}, extent = {{-20, -20}, {20, 20}}), iconTransformation(origin = {-100, -20}, extent = {{-20, -20}, {20, 20}})));
equation
  rover.r_0 = {r_rov[1],r_rov[2],r_rov[3]};
  rover.R.q = {q_rov[1],q_rov[2],q_rov[3],q_rov[4]};

  camera.r_0 = {r_cam[1],r_cam[2],r_cam[3]};
  camera.R.q = {q_cam[1],q_cam[2],q_cam[3],q_cam[4]};


annotation(
    uses(Modelica(version = "4.0.0")));
end Rover;
