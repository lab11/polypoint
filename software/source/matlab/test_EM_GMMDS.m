function [xy_hat_MDS_proc] = test_EM_GMMDS(D_hat, xy)

prm.n = size(D_hat,1);     % number of nodes
prm.d = 3;      % dimension
%prm.conn = 18;   % network connectivity
prm.T = size(D_hat,3);    % number of measurements
prm.plos = 0.3;
prm.k = 3;
prm.mu      = [0    2   2.4];
prm.sigma   = [0.2  0.4 0.3];
prm.alpha   = [0.5  0.3 0.2];

A = sum(D_hat > 0, 3) > 0;
prm.conn = sum(A(:));

% Generate network with required connectivity
%[xy, A] = create_graph(prm.n,prm.d,prm.conn);

% Sample measurements from the network with the required plos 
%[ LOS_Matrix,alpha_true,mu_true,sigma_true,D_true,D_hat] = initSimDistr(prm,xy,A);



%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% Make the measurement matrix symmetric (IMPORTANT!)
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%for i = 1 : size(D_hat,3)
%    D_hat(:,:,i) = nx_adjustDMatrix(D_hat(:,:,i));
%end


%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% Initialize the EM-GMMDS algorithm
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
xy_hat = xy + 14*randn(size(xy)); 
rep_CD = 3;
rep_EM = 50;
rep_MDS = 500;
rep_MDSi = 20;


% Run EM-GMMDS algorithm
tic;
[xhat,ahat,mhat,shat] = EM_GMMDS(D_hat,xy_hat,A,prm.k,rep_CD,rep_EM,rep_MDS,rep_MDSi);
toc;


% MDS
tic;
xhat_MDS = nx_MDS(D_hat,xy_hat,A,rep_CD*rep_MDS,rep_MDSi);
toc;


%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% Compute errors
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
RMSE = zeros(rep_CD*rep_MDS,1);
for rep = 1:rep_CD*rep_MDS
    [~,xy_hat] = procrustes(xy,xhat(:,:,rep), 'Scaling',false);
    RMSE(rep)= sqrt(mean(sum((xy-xy_hat).^2,2)));
end

% KLDE = zeros(rep_CD*rep_EM,1);
% for rep = 1:rep_CD*rep_EM
%     pt = 0;
%     for i = 1:prm.n
%         for j = (i+1):prm.n
%             if( A(i,j) == 1)
%                 err = gaussmixk(permute(mu_true(i,j,:),[3 1 2]),...
%                              permute(sigma_true(i,j,:),[3,1,2]),...
%                              permute(alpha_true(i,j,:),[3,1,2]), ...   
%                              permute(mhat(i,j,:,rep),[3 1 2 4]),...
%                              permute(shat(i,j,:,rep),[3 1 2 4]),...
%                              permute(ahat(i,j,:,rep),[3 1 2 4]));
%                 KLDE(rep) = (pt*KLDE(rep) + abs(err))/(pt+1);
%                 pt = pt+1;
%             end
%         end
%     end
% end

RMSE_MDS = zeros(rep_CD*rep_MDS,1);
for rep = 1:rep_CD*rep_MDS
    [~,xy_hat] = procrustes(xy,xhat_MDS(:,:,rep), 'Scaling',false);
    RMSE_MDS(rep)= sqrt(mean(sum((xy-xy_hat).^2,2)));
end
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%



%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%% Display
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
[~,xy_hat_proc] = procrustes(xy,xhat(:,:,end), 'Scaling',false);
[~,xy_hat_MDS_proc] = procrustes(xy,xhat_MDS(:,:,end), 'Scaling',false);
figure('position',[100 100 600 400],'color',[1 1 1]);
plot3(xy(:,1),xy(:,2),xy(:,3),'b+','MarkerSize',8,'LineWidth',3,'MarkerEdgeColor',[0 0.5 0.5]);
hold on;
plot3(xy_hat_proc(:,1),xy_hat_proc(:,2),xy_hat_proc(:,3),'ro','MarkerSize',6,'LineWidth',2);
plot3(xy_hat_MDS_proc(:,1),xy_hat_MDS_proc(:,2),xy_hat_MDS_proc(:,3),'mx','MarkerSize',6,'LineWidth',2);
%lines
for i= 1:prm.n
    for j = (i+1) : prm.n
        if A(i,j) == 1
            if( LOS_Matrix(i,j) == 1)
                plot3( [ xy(i,1), xy(j,1)],[ xy(i,2), xy(j,2)],[ xy(i,3), xy(j,3)],'k-');
            else
                plot3( [ xy(i,1), xy(j,1)],[ xy(i,2), xy(j,2)],[ xy(i,3), xy(j,3)],'k--');
            end
        end
    end
end
hold off
zlabel('meters');
grid on
legend('Ground Truth','GMMDS','MDS')
axis equal


figure('position',[700 100 600 400],'color',[1 1 1])
plot(RMSE,'r','linewidth',2)
xlabel('GMMDS Iterations')
ylabel('RMSE [m]')
hold on
plot(RMSE_MDS,'b','linewidth',2)
legend('GM-MDS','MDS');

 
% figure('color',[1 1 1])
% plot(KLDE,'b-','linewidth',2)
% xlabel('EM Iterations')
% ylabel('KL Divergece') 


