/*
 * File:   AIC.cpp
 * Author: Corrado Pezzato, TU Delft, DCSC
 *
 * Created on April 14th, 2019
 *
 * Class to perform active inference control of the 7DOF Franka Emika Panda robot.
 * Definition of the methods contained in AIC.h
 *
 */

#include "AIC.h"

  // Constructor which takes as argument the publishers and initialises the private ones in the class
  AIC::AIC(){
    // Initialize the variables for thr AIC
    AIC::initVariables();
  }
  AIC::~AIC(){}

  void   AIC::jointStatesCallback(const sensor_msgs::JointState::ConstPtr& msg)
  {
    // Save joint values
    for( int i = 0; i < 7; i++ ) {
      jointPos(i) = msg->position[i];
      jointVel(i) = msg->velocity[i];
    }
    // If this is the first time we read the joint states then we set the current beliefs
    if (dataReceived == 0){
      // Track the fact that the encoders published
      dataReceived = 1;
      // The first time we retrieve the position we define the initial beliefs about the states
      mu = jointPos;
      mu_p = jointVel;
    }

    // Generative model and its derivative from object grnMod of the class generativeModel
    g = genMod.getG(jointPos);
    gxprime = genMod.getGxprime(jointPos);
    gyprime = genMod.getGyprime(jointPos);
    gzprime = genMod.getGzprime(jointPos);

    // Simulate camera input from direct kinematics
    eev(0) = g(0) + 0.01*(((double) rand() / (RAND_MAX)));
    eev(1) = g(1) + 0.01*(((double) rand() / (RAND_MAX)));
    eev(2) = g(2) + 0.01*(((double) rand() / (RAND_MAX)));
    //std::cout << eev(1)-g(1) << std::endl;
    //T = AIC::getEEPose(jointPos);
    //std::cout << T(0,3)-g(0) << std::endl;
    //std::cout << T(1,3)-g(1) << std::endl;
    //std::cout << T(2,3)-g(2) << std::endl;
  }

  void   AIC::cameraCallback(const geometry_msgs::PoseStamped::ConstPtr& msg)
  {
    eePos.x = msg->pose.position.x;
    eePos.y = msg->pose.position.y;
    eePos.z = msg->pose.position.z;
    ROS_INFO("x = %f  y = %f  z = %f ", eePos.x-eev(0), eePos.y-eev(1), eePos.z-eev(2));
  }

  void   AIC::initVariables(){
    // Initialize publishers on the topics /panda_joint*_controller/command for the joint efforts
    tauPub1 = nh.advertise<std_msgs::Float64>("/panda_joint1_controller/command", 20);
    tauPub2 = nh.advertise<std_msgs::Float64>("/panda_joint2_controller/command", 20);
    tauPub3 = nh.advertise<std_msgs::Float64>("/panda_joint3_controller/command", 20);
    tauPub4 = nh.advertise<std_msgs::Float64>("/panda_joint4_controller/command", 20);
    tauPub5 = nh.advertise<std_msgs::Float64>("/panda_joint5_controller/command", 20);
    tauPub6 = nh.advertise<std_msgs::Float64>("/panda_joint6_controller/command", 20);
    tauPub7 = nh.advertise<std_msgs::Float64>("/panda_joint7_controller/command", 20);
    // Publisher for the free-energy
    IFE_pub = nh.advertise<std_msgs::Float64>("free_energy", 10);
    // Publishers for beliefs
    beliefs_mu_pub = nh.advertise<std_msgs::Float64MultiArray>("beliefs_mu", 10);
    beliefs_mu_p_pub = nh.advertise<std_msgs::Float64MultiArray>("beliefs_mu_p", 10);
    beliefs_mu_pp_pub = nh.advertise<std_msgs::Float64MultiArray>("beliefs_mu_pp", 10);

    // Subscribers for proprioceptive sensors (i.e. from joint:states) and camera position (i.e. aruco_single/pose)
    sensorSub = nh.subscribe("joint_states", 1, &AIC::jointStatesCallback, this);
    cameraSub = nh.subscribe("aruco_single/pose", 1, &AIC::cameraCallback, this);

    // Support variable
    dataReceived = 0;

    // Variances associated with the beliefs and the sensory inputs
    var_mu = 1.0;
    var_muprime = 1.0;
    var_q = 0.1;
    var_qdot = 0.1;
    var_eev = 1.0;

    // Learning rates for the gradient descent
    k_mu = 20;
    k_a = 1200;

    // Precision matrices (first set them to zero then populate the diagonal)
    SigmaP_yq0 = Eigen::Matrix<double, 7, 7>::Zero();
    SigmaP_yq1 = Eigen::Matrix<double, 7, 7>::Zero();
    SigmaP_yv0 = Eigen::Matrix<double, 7, 7>::Zero();
    SigmaP_mu = Eigen::Matrix<double, 7, 7>::Zero();
    SigmaP_muprime = Eigen::Matrix<double, 7, 7>::Zero();

    for( int i = 0; i < SigmaP_yq0.rows(); i = i + 1 ) {
      SigmaP_yq0(i,i) = 1/var_q;
      SigmaP_yq1(i,i) = 1/var_qdot;
      SigmaP_yv0(i,i) = 1/var_eev;
      SigmaP_mu(i,i) = 1/var_mu;
      SigmaP_muprime(i,i) = 1/var_muprime;
    }

    // Initialize control actions
    u << 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0;

    // Initialize prior beliefs about the second ordet derivatives of the states of the robot
    mu_pp << 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0;

    // Integration step
    h = 0.001;

    // Initialization of the DH parameters
    DH_a << 0.0, 0.0, 0.0825, -0.0825, 0.0, 0.088, 0.0;
    DH_d << 0.333, 0.0, 0.316, 0.0, 0.384, 0.0, 0.107;
    DH_alpha << M_PI_2, -M_PI_2, M_PI_2, -M_PI_2, M_PI_2, M_PI_2, 0;

    // Resize Float64MultiArray messages
    AIC_mu.data.resize(7);
    AIC_mu_p.data.resize(7);
    AIC_mu_pp.data.resize(7);
  }

  void AIC::minimiseF(){
    // Compute single sensory prediction errors
    SPEq = (jointPos.transpose()-mu.transpose())*SigmaP_yq0*(jointPos-mu);
    SPEdq = (jointVel.transpose()-mu_p.transpose())*SigmaP_yq1*(jointVel-mu_p);
    SPEv = var_eev*((eev(0)-g(0))*(eev(0)-g(0))+(eev(1)-g(1))*(eev(1)-g(1))+(eev(2)-g(2))*(eev(2)-g(2)));
    SPEmu_p = (mu_p.transpose()+mu.transpose()-mu_d.transpose())*SigmaP_mu*(mu_p+mu-mu_d);
    SPEmu_pp = (mu_pp.transpose()+mu_p.transpose())*SigmaP_muprime*(mu_pp+mu_p);

    // Free-energy as a sum of squared values (i.e. sum the SPE)
    F.data = SPEq + SPEdq + SPEv + SPEmu_p + SPEmu_pp;
    //ROS_INFO("%f", F);

    // Free-energy minimization using gradient descent and beliefs update
    mu_dot = mu_p - k_mu*(-SigmaP_yq0*(jointPos-mu)+SigmaP_mu*(mu_p+mu-mu_d)-SigmaP_yv0*(eev(0)-g(0))*gxprime -SigmaP_yv0*(eev(1)-g(1))*gyprime -SigmaP_yv0*(eev(2)-g(2))*gzprime);
    mu_dot_p = mu_pp - k_mu*(-SigmaP_yq1*(jointVel-mu_p)+SigmaP_mu*(mu_p+mu-mu_d)+SigmaP_muprime*(mu_pp+mu_p));
    mu_dot_pp = - k_mu*(SigmaP_muprime*(mu_pp+mu_p));

    // Belifs update
    mu = mu + h*mu_dot;             // Belief about the position
    mu_p = mu_p + h*mu_dot_p;       // Belief about motion of mu
    mu_pp = mu_pp + h*mu_dot_pp;    // Belief about motion of mu'

    // Publish beliefs as Float64MultiArray
    for (int i=0;i<7;i++){
       AIC_mu.data[i] = mu(i);
       AIC_mu_p.data[i] = mu_p(i);
       AIC_mu_pp.data[i] = mu_pp(i);
    }

    // Calculate and send control actions
    AIC::computeActions();

    // Publish free-energy
    IFE_pub.publish(F);
    // Publish beliefs
    beliefs_mu_pub.publish(AIC_mu);
    beliefs_mu_p_pub.publish(AIC_mu_p);
    beliefs_mu_pp_pub.publish(AIC_mu_pp);
  }

  void   AIC::computeActions(){
    // Compute control actions through gradient descent of F
    u = u-h*k_a*(SigmaP_yq1*(jointVel-mu_p)+SigmaP_yq0*(jointPos-mu));

    //SigmaP_yv0_real*(y.eev-[g.gx; g.gy])

    // Set the toques from u and publish
    tau1.data = u(0); tau2.data = u(1); tau3.data = u(2); tau4.data = u(3);
    tau5.data = u(4); tau6.data = u(5); tau7.data = u(6);
    // Publishing
    tauPub1.publish(tau1); tauPub2.publish(tau2); tauPub3.publish(tau3);
    tauPub4.publish(tau4); tauPub5.publish(tau5); tauPub6.publish(tau6);
    tauPub7.publish(tau7);
  }

  int AIC::dataReady(){
    // Method to control if the joint states have been received already, used in the main function
    if(dataReceived==1)
      return 1;
    else
      return 0;
  }

  void AIC::setGoal(std::vector<double> desiredPos){
    for(int i=0; i<desiredPos.size(); i++){
      mu_d(i) = desiredPos[i];
    }
  }

  // Compute the direct kinematics
  Eigen::Matrix<double, 4, 4> AIC::getEEPose(Eigen::Matrix<double, 7, 1> theta){
    // Initialize transformation matrix for the end effector DH_T
    DH_T = Eigen::Matrix<double, 4, 4>::Identity();
    // Compute DK
    theta(1) = -theta(1);
    for(int k=0; k<7; k++){
      DH_A << cos(theta(k)), -sin(theta(k))*cos(DH_alpha(k)), sin(theta(k))*sin(DH_alpha(k)), DH_a(k)*cos(theta(k)),
              sin(theta(k)), cos(theta(k))*cos(DH_alpha(k)), -cos(theta(k))*sin(DH_alpha(k)), DH_a(k)*sin(theta(k)),
                   0.0,                sin(DH_alpha(k)),              cos(DH_alpha(k)),               DH_d(k),
                   0.0,                      0.0,                            0.0,                      1.0;
      // Trasfrmation to the joint7
      DH_T = DH_T*DH_A;
    }
    return(DH_T);
  }