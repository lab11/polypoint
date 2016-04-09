function [Lam,lambda,Lmean,lambda_mode,lambda_1,lambda_2,beta,Gam,gamma_0,Kgamma, ...
sigma_cluster,nlos,gamma_rise,gamma_1,chi,m0,Km,sigma_m0,sigma_Km, ...
sfading_mode,m0_sp,std_shdw,kappa,fc,fs] = uwb_sv_params_15_4a( cm_num )
% Written by Sun Xu, Kim Chee Wee, B. Kannan & Francois Chin on 14/09/2004
% Return modified S-V model parameters for standard UWB channel models
%————————————————————————–
% Lam Cluster arrival rate (clusters per nsec)
% lambda Ray arrival rate (rays per nsec)
% Lmean Mean number of Clusters
% lambda_mode Flag for Mixture of poission processes for ray arrival times
%0-> Poisson process for the ray arrival times
%1-> Mixture of poission processes for the ray arrival times
%2-> tapped delay line model
% lambda_1 Ray arrival rate for Mixture of poisson processes (rays per nsec)
% lambda_2 Ray arrival rate for Mixture of poisson processes (rays per nsec)
% beta Mixture probability
%————————————————————————–
% Gam Cluster decay factor (time constant, nsec)
% gamma0 Ray decay factor (time constant, nsec)
% Kgamma Time dependence of ray decay factor
% sigma_cluster Standard deviation of normally distributed variable for cluster energy
% nlos Flag for non line of sight channel
%0-> LOS
%1-> NLOS with first arrival path starting at t ~= 0
%2-> NLOS with first arrival path starting at t = 0 and diffused first cluster
% gamma_rise Ray decay factor of diffused first cluster (time constant, nsec)
% gamma_1 Ray decay factor of diffused first cluster (time constant, nsec)
% chi Diffuse weight of diffused first cluster
%————————————————————————–
% m0 Mean of log-normal distributed nakagami-m factor
% Km Time dependence of m0
% sigma_m0 Standard deviation of log-normal distributed nakagami-m factor
% sigma_Km Time dependence of sigma_m0
% sfading_mode Flag for small-scale fading
%0-> All paths have same m-factor distribution
%1-> LOS first path has a deterministic large m-factor
%2-> LOS first path of each cluster has a deterministic
% large m-factor
% m0_sp Deterministic large m-factor
%————————————————————————–
% std_shdw Standard deviation of log-normal shadowing of entire impulse response
%————————————————————————–
% kappa Frequency dependency of the channel
%————————————————————————–
% fc Center Frequency
% fs Frequency Range %
% modified by I2R
if cm_num == 1, % Residential LOS
% MPC arrival
Lam = 0.047; lambda = NaN; Lmean = 3;
lambda_mode = 1;
lambda_1 = 1.54; lambda_2 = 0.15; beta = 0.095;
% MPC decay
Gam = 22.61; gamma_0 = 12.53; Kgamma = 0; sigma_cluster = 2.75;
nlos = 0;
gamma_rise = NaN; gamma_1 = NaN; chi = NaN; % dummy in this scenario
% Small-scale Fading
m0 = 0.67; Km = 0; sigma_m0 = 0.28; sigma_Km = 0;
sfading_mode = 0; m0_sp = NaN;
% Large-scale Fading – Shadowing
std_shdw = 2.22;
% Frequency Dependence
kappa = 1.12;
fc = 6; % GHz
fs = 8; % 2 - 10 GHz
elseif cm_num == 2, % Residential NLOS
% MPC arrival
Lam = 0.12; lambda = NaN; Lmean = 3.5;
lambda_mode = 1;
lambda_1 = 1.77; lambda_2 = 0.15; beta = 0.045;
% MPC decay
Gam = 26.27; gamma_0 = 17.5; Kgamma = 0; sigma_cluster = 2.93;
nlos = 1;
gamma_rise = NaN; gamma_1 = NaN; chi = NaN; % dummy in this scenario
% Small-scale Fading
m0 = 0.69; Km = 0; sigma_m0 = 0.32; sigma_Km = 0;
sfading_mode = 0; m0_sp = NaN;
% Large-scale Fading – Shadowing
std_shdw = 3.51;
% Frequency Dependence
kappa = 1.53;
fc = 6; % GHz
fs = 8; % 2 - 10 GHz
elseif cm_num == 3, % Office LOS
% MPC arrival
Lam = 0.016; lambda = NaN; Lmean = 5.4;
lambda_mode = 1;
lambda_1 = 0.19; lambda_2 = 2.97; beta = 0.0184;
% MPC decay
Gam = 14.6; gamma_0 = 6.4; Kgamma = 0; sigma_cluster = 3; % assumption
nlos = 0;
gamma_rise = NaN; gamma_1 = NaN; chi = NaN; % dummy in this scenario
% Small-scale Fading
m0 = 0.42; Km = 0; sigma_m0 = 0.31; sigma_Km = 0;
sfading_mode = 2; m0_sp = 3; % assumption
% Large-scale Fading – Shadowing
std_shdw = 0; %1.9;
% Frequency Dependence
kappa = -3.5;
fc = 4.5; % GHz
fs = 3; % 3 - 6 GHz
elseif cm_num == 4, % Office NLOS
% MPC arrival
Lam = 0.19; lambda = NaN; Lmean = 3.1;
lambda_mode = 1;
lambda_1 = 0.11; lambda_2 = 2.09; beta = 0.0096;
% MPC decay
Gam = 19.8; gamma_0 = 11.2; Kgamma = 0; sigma_cluster = 3; % assumption
nlos = 2;
gamma_rise = 15.21; gamma_1 = 11.84; chi = 0.78;
% Small-scale Fading
m0 = 0.5; Km = 0; sigma_m0 = 0.25; sigma_Km = 0;
sfading_mode = 0; m0_sp = NaN; % assumption
% Large-scale Fading – Shadowing
std_shdw = 3.9;
% Frequency Dependence
kappa = 5.3;
fc = 4.5; % GHz
fs = 3; % 3 - 6 GHz
elseif cm_num == 5, % Outdoor LOS
% MPC arrival
Lam = 0.0448; lambda = NaN; Lmean = 13.6;
lambda_mode = 1;
lambda_1 = 0.13; lambda_2 = 2.41; beta = 0.0078;
% MPC decay
Gam = 31.7; gamma_0 = 3.7; Kgamma = 0; sigma_cluster = 3; % assumption
nlos = 0;
gamma_rise = NaN; gamma_1 = NaN; chi = NaN; % dummy in this scenario
% Small-scale Fading
m0 = 0.77; Km = 0; sigma_m0 = 0.78; sigma_Km = 0;
sfading_mode = 2; m0_sp = 3; % assumption
% Large-scale Fading – Shadowing
std_shdw = 0.83;
% Frequency Dependence
kappa = -1.6;
fc = 4.5; % GHz
fs = 3; % 3 - 6 GHz
elseif cm_num == 6, % Outdoor NLOS
% MPC arrival
Lam = 0.0243; lambda = NaN; Lmean = 10.5;
lambda_mode = 1;
lambda_1 = 0.15; lambda_2 = 1.13; beta = 0.062;
% MPC decay
Gam = 104.7; gamma_0 = 9.3; Kgamma = 0; sigma_cluster = 3; % assumption
nlos = 1;
gamma_rise = NaN; gamma_1 = NaN; chi = NaN; % dummy in this scenario
% Small-scale Fading
m0 = 0.56; Km = 0; sigma_m0 = 0.25; sigma_Km = 0;
sfading_mode = 0; m0_sp = NaN; % assumption
% Large-scale Fading – Shadowing
std_shdw = 2; % assumption
% Frequency Dependence
kappa = 0.4;
fc = 4.5; % GHz
fs = 3; % 3 - 6 GHz
elseif cm_num == 7, % Industrial LOS
% MPC arrival
Lam = 0.0709; lambda = NaN; Lmean = 4.75; % lambda is assumption
lambda_mode = 2;
lambda_1 = NaN; lambda_2 = NaN; beta = NaN; % dummy in this scenario
% MPC decay
Gam = 3.1; gamma_0 = 0.15; Kgamma = 0.21; sigma_cluster = 4.32;
nlos = 0;
gamma_rise = NaN; gamma_1 = NaN; chi = NaN; % dummy in this scenario
% Small-scale Fading
m0 = 0.36; Km = 0; sigma_m0 = 1.13; sigma_Km = 0;
sfading_mode = 1; m0_sp = 12.99;
% Large-scale Fading – Shadowing
std_shdw = 6;
% Frequency Dependence
kappa = -5.6;
fc = 5; % GHz
fs = 6; % 2 - 8 GHz
elseif cm_num == 8, % Industrial NLOS
% MPC arrival
Lam = 0.089; lambda = NaN; Lmean = 1; % lambda is assumption
lambda_mode = 2;
lambda_1 = NaN; lambda_2 = NaN; beta = NaN; % dummy in this scenario
% MPC decay
Gam = 5.83; gamma_0 = 0.3; Kgamma = 0.44; sigma_cluster = 2.88;
nlos = 2;
gamma_rise = 4; gamma_1 = 19.7; chi = 0.99;
% Small-scale Fading
m0 = 0.3; Km = 0; sigma_m0 = 1.15; sigma_Km = 0;
sfading_mode = 0; m0_sp = NaN; % m0_sp is assumption
% Large-scale Fading – Shadowing
std_shdw = 6;
% Frequency Dependence
kappa = -7.82;
fc = 5; % GHz
fs = 6; % 2 - 8 GHz
elseif cm_num == 9, % Open Outdoor Environment NLOS (Fram, Snow-Covered Open Area)
% MPC arrival
Lam = 0.0305; lambda = 0.0225; Lmean = 3.31;
lambda_mode = 0;
lambda_1 = NaN; lambda_2 = NaN; beta = NaN; % dummy in this scenario
% MPC decay
Gam = 56; gamma_0 = 0.92; Kgamma = 0; sigma_cluster = 3; % sigma_cluster is assumption
nlos = 1;
gamma_rise = NaN; gamma_1 = NaN; chi = NaN;
% Small-scale Fading
m0 = 4.1; Km = 0; sigma_m0 = 2.5; sigma_Km = 0;
sfading_mode = 0; m0_sp = NaN; % m0_sp is assumption
% Large-scale Fading – Shadowing
std_shdw = 3.96;
% Frequency Dependence
kappa = -1; % Kappa is assumption
fc = 5; % GHz
fs = 6; % 2 - 8 GHz
else
error('cm_num is wrong!!')
end
return
