#include "model.h"
#include "load.h"
#include "template_matching.h"
#include <math.h> 
#include <limits>
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <random>


//=============================================
//Helper functions
double IN(double x){ //Standard Normal PDF 
	return exp(-pow(x,2)*0.5)/sqrt(2*M_PI);
	
}
double IC(double x){ //Standard Normal CDF
	return 0.5*(1+erf(x/sqrt(2)));
}


double R(double x){ //Mills Ratio
	if (x > 8){
		return 1.0 / x;
	}
	double N = IC(x);
	double D = IN(x);
	if (D < pow(10,-15)){ //machine epsilon
		return 1.0 / pow(10,-15);
	}
	return exp(log(1. - N)-log(D));
}

bool checkNumber(double x){
	if (isfinite(x)){
		return true;
	}
	return false;

}
//=============================================
// Uniform Noise Class
double NOISE::pdf(double x, int strand){
	if (strand == 1){
		return (w*pi) / abs(b-a);
	}
	return (w*(1-pi)) / abs(b-a);
}
NOISE::NOISE(){}
NOISE::NOISE(double A, double B, double W, double PI){
	a=A;
	b=B;
	w=W;
	pi=PI;
}

//=============================================
//Uniform Class
double UNI::pdf(double x, int strand){ //probability density function
	double p;

	if ( a<= x and x <=b){

		p= w / abs(b- a);
		p= p*pow(pi, max(0, strand) )*pow(1.-pi, max(0, -strand) );
		return p;
	}
	return 0;
}

string UNI::print(){
	string text = ("U: " + to_string(a) + "," + to_string(b) 
	+ "," + to_string(w) + "," + to_string(pi));
	return text;
}	
UNI::UNI(double start, double stop, double w_i, int strand, int POS, double Pi){
	a 		= start;
	b 		= stop;
	w 		= w_i;
	st 		= strand;
	pos 	= POS;
	if (st==1){
		pi=1;
	}else{
		pi=0;
	}
	//===================
	//this oversets the constraint that uniform must take either 
	//forward or reverse data points
	pi 		= Pi;
	//===================
	delta_a=0;
	delta_b=0;
	ri_forward=0, ri_reverse=0;


}

UNI::UNI(){} //empty constructor


//=============================================
//Exponentially Modified Gaussian class

string EMG::print(){
	string text 	= ("N: " + to_string(mu)+","+to_string(si)
		+ "," + to_string(l) + "," + to_string(w) + "," + to_string(pi));
	return text;
}

EMG::EMG(){}//empty constructor

EMG::EMG(double MU, double SI, double L, double W, double PI ){
	mu 	= MU;
	si 	= SI;
	l  	= L;
	w 	= W;
	pi 	= PI;
}


double EMG::pdf(double z, int s ){
	double vl 		= (l/2)*(s*2*(mu-z) + l*pow(si,2));
	double p;
	if (vl > 100){ //potential for overflow, inaccuracies
		p 			= l*IN((z-mu)/si)*R(l*si - s*((z-mu)/si));
	}else{
		p 			= (l/2)*exp(vl)*erfc((s*(mu-z) + l*pow(si ,2))/(sqrt(2)*si));
	}
	vl 				= p*w*pow(pi, max(0, s) )*pow(1.-pi, max(0, -s) );
	if (checkNumber(vl)){
		return vl;
	}
	return 0.;

}
double EMG::EY(double z, int s){
	return max(0. , s*(z-mu) - l*pow(si, 2) + (si / R(l*si - s*((z-mu)/si))));
}
double EMG::EY2(double z, int s){

	return pow(l,2)*pow(si,4) + pow(si, 2)*(2*l*s*(mu-z)+1 ) + pow(mu-z,2) - ((si*(l*pow(si,2) + s*(mu-z)))/R(l*si - s*((z-mu)/si) ));
	
}

//=========================================================
//components wrapper class for EMG and UNIFORM objects

component::component(){//empty constructor
} 

void component::set_priors(double s_0, double s_1, 
	double l_0, double l_1, double w_0,double strand_0){
	//============================
	//for sigma
	alpha_0 	= 20.46;
	beta_0 		= 10.6;
	//============================
	//for lambda
	alpha_1 	= 20.823;
	beta_1 		= 0.5;
	//==============================
	//for initial length of Uniforms
	alpha_2 	= 1.297;
	beta_2 		= 8260;

	//*****************************************************
	//Priors on parameters for MAP Estimate
	ALPHA_0 = s_0, BETA_0 =s_1; //for sigma
	ALPHA_1 = l_0, BETA_1 =l_1; //for lambda
	ALPHA_2 = w_0; //for weights, dirchlet
	ALPHA_3 = strand_0; //for strand probs

	
}

int get_nearest_position(segment * data, double center, double dist){
	int i;

	if (dist < 0 ){
		i=0;
		while (i < data->XN and (data->X[0][i] -center) < dist){
			i++;
		}
	}else{
		i=data->XN-1;
		while (i >=0 and (data->X[0][i] - center) > dist){
			i--;
		}
	}
	return i;
}
bool check_uniform_support(component c, int forward){
	if (forward==1){
		if (c.forward.b < (c.bidir.mu + c.bidir.si + (1.0 / c.bidir.l) )){
			return false;
		}
		return true;
	}
	else{
		if (c.forward.a < (c.bidir.mu - c.bidir.si - (1.0 / c.bidir.l) )){
			return false;
		}
		return true;

	}
}


void component::initialize(double mu, segment * data , int K, double scale, double noise_w, 
	double noise_pi){//random seeds...
	EXIT=false;
	if (noise_w>0){
		noise 	= NOISE(data->minX, data->maxX, 
			noise_w, noise_pi);
		type 	= 0; 
	}else{
		//====================================
		random_device rd;
		mt19937 mt(rd());
		
		double sigma,lambda, pi_EMG, w_EMG  ;	
		double b_forward,  w_forward;
		double a_reverse,  w_reverse;

		//====================================
		//start sampling
		//for the bidirectional/EMG component
		gamma_distribution<double> dist_sigma(alpha_0,beta_0);
		gamma_distribution<double> dist_lambda(alpha_1,beta_1);
		uniform_real_distribution<double> dist_lambda_2(1, 1000);
		uniform_real_distribution<double> dist_sigma_2(1, 100);
		gamma_distribution<double> dist_lengths(1,( (data->maxX-data->minX)/(K)));
		
		sigma 		= dist_sigma_2(mt)/scale;
		lambda 		= scale/dist_lambda_2(mt) ;
		double dist = (1.0/lambda) + sigma + dist_lengths(mt);
		int j 		= get_nearest_position(data, mu, dist);
		
		b_forward 	= data->X[0][j];
		if (b_forward < (mu+(1.0/lambda)) ){
			forward 	= UNI(mu+(1.0/lambda), data->maxX, 0., 1, j, 0.5);
		}
		else{	
			forward 	= UNI(mu+(1.0/lambda), b_forward, 1.0 / (3*K), 1, j, 0.5);
		}	
		dist 		= (-1.0/lambda) - sigma - dist_lengths(mt);
		j 			= get_nearest_position(  data, mu, dist);
		
		a_reverse 	= data->X[0][j];

		bidir 		= EMG(mu, sigma, lambda, 1.0 / (3*K), 0.5);
		if (a_reverse > mu-(1.0/lambda) ){
			reverse 	= UNI(data->minX, mu-(1.0/lambda), 0., -1, j,0.5);
		}else{
			reverse 	= UNI(a_reverse, mu-(1.0/lambda), 1.0 / (3*K), -1, j,0.5);
		}
		type 		= 1;
	}

} 

void component::print(){
	if (type==1){
		string text 	= bidir.print()+ "\n";
		text+=forward.print()+ "\n";
		text+=reverse.print() + "\n";
		cout<<text;
	}else{
		cout<<"NOISE: " << noise.w<<"," <<noise.pi<<endl;
	}
}

string component::write_out(){
	if (type==1){
		string text 	= bidir.print()+ "\n";
		text+=forward.print()+ "\n";
		text+=reverse.print() + "\n";

		return text;
	}
	return "";
}



double component::evaluate(double x, int st){
	if (type ==0){ //this is the uniform noise component
		return noise.pdf(x, st);
	}
	if (st==1){
		bidir.ri_forward 	= bidir.pdf(x, st);
		forward.ri_forward 	= forward.pdf(x, st);
		reverse.ri_forward 	= reverse.pdf(x,st);
		return bidir.ri_forward + forward.ri_forward + reverse.ri_forward;
	}
	bidir.ri_reverse 	= bidir.pdf(x, st);
	reverse.ri_reverse 	= reverse.pdf(x, st);
	forward.ri_reverse 	= forward.pdf(x, st);
	return bidir.ri_reverse + reverse.ri_reverse + forward.ri_reverse;
}
double component::pdf(double x, int st){
	if (type==0){
		return noise.pdf(x,st);
	}
	if (st==1){
		return bidir.pdf(x,st) + forward.pdf(x, st);
	}
	return bidir.pdf(x,st) + reverse.pdf(x,st);
}


void component::add_stats(double x, double y, int st, double normalize){
	if (type==0){//noise component
		if (st==1){
			noise.r_forward+=(y*noise.ri_forward/normalize);
			noise.ri_forward=0;
		}else{
			noise.r_reverse+=(y*noise.ri_reverse/normalize);
			noise.ri_reverse=0;
		}

	}else{
		double vl, vl2, vl3;
		if (st==1){
			vl 	= bidir.ri_forward / normalize;
			vl2 = forward.ri_forward/normalize;
			vl3 = reverse.ri_forward/normalize;
			bidir.ri_forward=0, forward.ri_forward=0;
			bidir.r_forward+=(vl*y);
			forward.r_forward+=(vl2*y);
			reverse.r_forward+=(vl3*y);
		
		}else{
			vl 	= bidir.ri_reverse / normalize;
			vl2 = reverse.ri_reverse / normalize;
			vl3 = forward.ri_reverse / normalize;
			bidir.ri_reverse=0, reverse.ri_reverse=0;
			bidir.r_reverse+=(vl*y);

			reverse.r_reverse+=(vl2*y);
			forward.r_reverse+=(vl3*y);
		}
		//now adding all the conditional expections for the convolution
		double current_EY 	= bidir.EY(x, st);
		double current_EY2 	= bidir.EY2(x, st);
		double current_EX 	= x-(st*current_EY);
		bidir.ey+=current_EY*vl*y;
		bidir.ex+=current_EX*vl*y;
		bidir.ex2+=(pow(current_EX,2) + current_EY2 - pow(current_EY,2))*vl*y;	
	}
	
}

void component::reset(){
	if (type){
		bidir.ey=0, bidir.ex=0, bidir.ex2=0, bidir.r_reverse=0, bidir.r_forward=0;
		bidir.ri_forward=0, forward.ri_forward=0, forward.ri_reverse=0;
		bidir.ri_reverse=0, reverse.ri_reverse=0, reverse.ri_forward=0;
		forward.r_forward=0, forward.r_reverse=0, reverse.r_reverse=0, reverse.r_forward=0;
		forward.delta_a=0, forward.delta_b=0, reverse.delta_a=0, reverse.delta_b=0;
	}else{
		noise.r_forward=0,noise.r_reverse=0;
		noise.ri_reverse=0,noise.ri_forward=0 ;
		
	}
}

double component::get_all_repo(){
	if (type==1){
		return bidir.r_forward+bidir.r_reverse+forward.r_forward+reverse.r_reverse;
	}
	return noise.r_forward+noise.r_reverse;

}

void component::update_parameters(double N, int K){
	if (type==1){
		//first for the bidirectional
		double r 	= bidir.r_forward + bidir.r_reverse;
		bidir.pi 	= (bidir.r_forward + ALPHA_3) / (r + ALPHA_3*2);
		bidir.w 	= (r + ALPHA_2) / (N + ALPHA_2*K*3 + K*3) ;
		bidir.mu 	= bidir.ex / (r+0.001);

		
		bidir.si 	= pow(abs((1. /(r + 3 + ALPHA_0 ))*(bidir.ex2-2*bidir.mu*bidir.ex + 
			r*pow(bidir.mu,2) + 2*BETA_0  )), 0.5) ;
		if (bidir.si >= 10.){//this component is blowing up
			EXIT 	= true;
		}
		bidir.l 	= min((r+ALPHA_1) / (bidir.ey + ALPHA_1), 4.);
		if (bidir.l < 0.05){//this component is blowing up
			EXIT 	= true;
		}
		//now for the forward and reverse strand elongation components
		forward.w 	= (forward.r_forward + ALPHA_2) / (N+ ALPHA_2*K*3 + K*3);
		reverse.w 	= (reverse.r_reverse + ALPHA_2) / (N+ ALPHA_2*K*3 + K*3);
		forward.a 	= bidir.mu + (1.0 /bidir.l), reverse.b=bidir.mu - (1.0 / bidir.l);
		//update PIS, this is obviously overwritten if we start the EM seeder with 0/1
		forward.pi 	= (forward.r_forward + 1) / (forward.r_forward + forward.r_reverse+2);
		reverse.pi 	= (reverse.r_forward + 1)/ (reverse.r_forward + reverse.r_reverse+2);


	}
}

bool component::check_elongation_support(){
	if (forward.b <=forward.a and bidir.mu==0){
		return true;
	}
	else if(reverse.b <= reverse.a and bidir.mu==0){
		return true;
	}
	return false;
}


void component::initialize_with_parameters(vector<double> init_parameters, segment * data, int K){
	if (init_parameters.empty()){
		noise 	= NOISE(data->minX, data->maxX, 0.01, 0.5);
		type 	= 0; 
	}else{
		double mu 	= init_parameters[0];
		double si 	= init_parameters[1];
		double l 	= init_parameters[2];
		double pi 	= init_parameters[3];
		bidir 		= EMG(mu, si, l, 1.0 / (3*K), pi);//bidir component
		//now choose supports of forward and reverse
		random_device rd;
		mt19937 mt(rd());
		gamma_distribution<double> dist_lengths(1,( (data->maxX-data->minX)/(K)));
			
		double dist 		= (1.0/l) + si + dist_lengths(mt);
		int j 				= get_nearest_position(data, mu, dist);	
		double b_forward 	= data->X[0][j];
		if (b_forward < (mu+(1.0/l)) ){
			forward 	= UNI(mu+(1.0/l), data->maxX, 0., 1, j, 1);
		}
		else{	
			forward 	= UNI(mu+(1.0/l), b_forward, 1.0 / (3*K), 1, j, 1);
		}	
				
		dist 				= (-1.0/l) - si - dist_lengths(mt);
		j 					= get_nearest_position(  data, mu, dist);
		double a_reverse 	= data->X[0][j];

		
		if (a_reverse > mu-(1.0/l) ){
			reverse 	= UNI(data->minX, mu-(1.0/l ), 0., -1, j,0.);
		}else{
			reverse 	= UNI(a_reverse, mu-(1.0/l ), 1.0 / (3*K), -1, j,0.);
		}
		type 		= 1;
	}	
}


//=========================================================
//FIT function this where we are running EM rounds
//=========================================================
//helper functions for fit

double sum(double * X, int N){
	double vl=0;
	for (int i = 0; i < N; i++){
		vl+=X[i];
	}
	return vl;

}
double LOG(double x){
	if (x <= 0){
		return nINF;
	}
	return log(x);
}


double calc_log_likelihood(component * components, int K, segment * data){
	double ll 	= 0;
	double forward, reverse;
	for (int i = 0 ; i < data->XN; i++){
		forward=0, reverse=0;
		for (int k = 0; k < K; k++){
			forward+=(components[k].pdf(data->X[0][i], 1));
			reverse+=(components[k].pdf(data->X[0][i], -1));
		}
		ll+=LOG(forward)*data->X[1][i]; 
		ll+=LOG(reverse)*data->X[2][i];
	}
	return ll;
}	
int get_direction(uniform_int_distribution<int> direction,mt19937 mt){
	if (direction(mt)==0){
		return 1;
	}
	return -1;
}
int get_new_position(geometric_distribution<int> dist_uni, mt19937 mt, 
	int pos, int N, int direction, int st, segment * data){
	int ct 	= dist_uni(mt)+1;
	int i = pos;
	int j = 0;
	if (direction==1){
		while (i < (N-1) and j < ct){
			if (data->X[st][i] > 0){
				j++;
			}
			i++;
		}
	}else{
		while (i > 0 and j < ct){
			if (data->X[st][i]>0){
				j++;
			}
			i--;
		}
	}
	return i;
}

double move_uniforom_support(component * components, int K, int add, 
	segment * data, double move, double base_ll){
	//===========================================================================
	//normal distribution centered around 0 and some variance, how much to move 
	//uniform supports
	random_device rd;
	mt19937 mt(rd());
	geometric_distribution<int> dist_uni(0.9);
	uniform_int_distribution<int> direction(0,1);
	int 	steps[K][2];
	double  new_bounds[K][2];
	double ll;
	double prev_a, prev_b;
	for (int k = 0; k < K; k++){
		prev_b=components[k].forward.b, prev_a=components[k].reverse.a;
		steps[k][0] 	= get_new_position(dist_uni, mt, 
			components[k].forward.pos, data->XN, get_direction(direction, mt), 1, data);
		steps[k][1] 	= get_new_position(dist_uni, mt, 
			components[k].reverse.pos, data->XN, get_direction(direction, mt), 2, data);
		//firs the forward
		components[k].forward.b=data->X[0][steps[k][0]];
		if (check_uniform_support(components[k], 1)){
			ll 	= calc_log_likelihood(components, K+add, data);
			//printf("%f,%f,%f,%f\n", prev_b, components[k].forward.b, base_ll,ll );
			if (ll > base_ll){
				new_bounds[k][0] 	= components[k].forward.b;
			}else{
				new_bounds[k][0] 	= prev_b;
				steps[k][0] 		= components[k].forward.pos;
			}
		}else{
			new_bounds[k][0] 	= prev_b;
			steps[k][0] 		= components[k].forward.pos;
		}
		components[k].forward.b 	= prev_b;
		//now reverse
		components[k].reverse.a=data->X[0][steps[k][1]];
		if (check_uniform_support(components[k], 0)){

			ll 	= calc_log_likelihood(components, K+add, data);
			if (ll > base_ll){
				new_bounds[k][1] 	= components[k].reverse.a;
			}else{
				new_bounds[k][1] 	= prev_a;
				steps[k][1] 		= components[k].reverse.pos;
			}
			//printf("%f,%f,%f,%f\n", prev_a, components[k].reverse.a, base_ll,ll );
		}else{
				new_bounds[k][1] 	= prev_a;
				steps[k][1] 		= components[k].reverse.pos;			
		}
		components[k].reverse.a 	= prev_a;
	}
	for (int k =0; k < K;k++){
		components[k].forward.pos 	= steps[k][0];
		components[k].forward.b 	= new_bounds[k][0];
		components[k].reverse.pos 	= steps[k][1];
		components[k].reverse.a 	= new_bounds[k][1];
 		
			
	}



	ll 		= calc_log_likelihood(components, K+add, data);
	return ll;
}

void update_weights_only(component * components, segment * data, int K, int add){
	double weights[K][3];
	double current[K][3][2];
	double norms[2];
	double weights_noise=0;
	for (int k = 0; k < K; k++){
		weights[k][0] 	= 0,weights[k][1] 	= 0,weights[k][2] 	= 0;
	}
	for (int i = 0; i < data->XN; i++){
		norms[0] = 0, norms[1] = 0;
		for (int k =0; k < K; k++ ){
			current[k][0][0] 	= components[k].bidir.pdf(data->X[0][i],1 );
			current[k][0][1] 	= components[k].bidir.pdf(data->X[0][i],-1 );

			current[k][1][0] 	= components[k].forward.pdf(data->X[0][i],1 );
			current[k][1][1] 	= components[k].forward.pdf(data->X[0][i],-1 );

			current[k][2][0] 	= components[k].reverse.pdf(data->X[0][i],1 );
			current[k][2][1] 	= components[k].reverse.pdf(data->X[0][i],-1 );



			norms[0]+=(current[k][0][0]+current[k][1][0]+current[k][2][0]);
			norms[1]+=(current[k][0][1]+current[k][1][1]+current[k][2][1]);
		}
		//add the noise component
		if (add){
			norms[0]+= components[K].noise.pdf(data->X[0][i], 1);
			norms[1]+= components[K].noise.pdf(data->X[0][i], -1);
		}
		for (int s = 0; s < 2; s++){
			if (norms[s]>0){
				for (int k = 0; k < K;k++){
					weights[k][0] += (current[k][0][s]/norms[s])*data->X[s+1][i];
					weights[k][1] += (current[k][1][s]/norms[s])*data->X[s+1][i];
					weights[k][2] += (current[k][2][s]/norms[s])*data->X[s+1][i];			
				}
				if (s == 0){
					weights_noise 	+= (components[K].noise.pdf(data->X[0][i], 1)/norms[s])*data->X[s+1][i];
				}else{
					weights_noise 	+= (components[K].noise.pdf(data->X[0][i], -1)/norms[s])*data->X[s+1][i];	
				}
			}
		}
	}
	double N=0;
	for (int k =0; k < K;k++){
		N+=(weights[k][0]+weights[k][1]+weights[k][2]);
	}
	N+=weights_noise;
	for (int k =0; k < K;k++){
		components[k].bidir.w 		= weights[k][0]/N;
		components[k].forward.w 	= weights[k][1]/N;
		components[k].reverse.w 	= weights[k][2]/N;		
	}

}



double move_uniforom_support2(component * components, int K, int add, 
	segment * data, double move, double base_ll){
	//===========================================================================
	//normal distribution centered around 0 and some variance, how much to move 
	//uniform supports
	random_device rd;
	mt19937 mt(rd());
	geometric_distribution<int> dist_uni(0.3);
	uniform_int_distribution<int> direction(0,1);
	int 	steps[K][2];
	double  new_bounds[K][2];
	double  weights[K][3];
	double ll;
	double prev_a, prev_b, prev_w_forward, prev_w_reverse, prev_w_bidir;
	for (int k = 0; k < K; k++){
		prev_b=components[k].forward.b, prev_a=components[k].reverse.a;
		steps[k][0] 	= get_new_position(dist_uni, mt, 
			components[k].forward.pos, data->XN, get_direction(direction, mt), 1, data);
		steps[k][1] 	= get_new_position(dist_uni, mt, 
			components[k].reverse.pos, data->XN, get_direction(direction, mt), 2, data);
		//firs the forward
		components[k].forward.b=data->X[0][steps[k][0]];
		prev_w_bidir 			= components[k].bidir.w;
		prev_w_forward 			= components[k].forward.w;
		prev_w_reverse 			= components[k].reverse.w;
		if (check_uniform_support(components[k], 1)){
			//now we need to reupdate weight parameters...

			update_weights_only(components, data, K, add);
			ll 	= calc_log_likelihood(components, K+add, data);
			//printf("ll: %f, base_ll: %f\n", ll, base_ll);

			//printf("%f,%f,%f,%f\n", prev_b, components[k].forward.b, base_ll,ll );
			if (ll > base_ll){
				new_bounds[k][0] 	= components[k].forward.b;
				base_ll 			= ll;

			}else{
				new_bounds[k][0] 	= prev_b;
				steps[k][0] 		= components[k].forward.pos;
			}
		}else{
			new_bounds[k][0] 	= prev_b;
			steps[k][0] 		= components[k].forward.pos;

		}
		components[k].bidir.w 	= prev_w_bidir;
		components[k].forward.w = prev_w_forward;
		components[k].reverse.w = prev_w_reverse;
		
		components[k].forward.b 	= prev_b;
		//now reverse
		components[k].reverse.a=data->X[0][steps[k][1]];
		if (check_uniform_support(components[k], 0)){
			update_weights_only(components, data, K, add);
			ll 	= calc_log_likelihood(components, K+add, data);
			//printf("ll: %f, base_ll: %f\n", ll, base_ll);
			if (ll > base_ll){
				new_bounds[k][1] 	= components[k].reverse.a;
				base_ll 			= ll;
			}else{
				new_bounds[k][1] 	= prev_a;
				steps[k][1] 		= components[k].reverse.pos;
			}
			//printf("%f,%f,%f,%f\n", prev_a, components[k].reverse.a, base_ll,ll );
		}else{
				new_bounds[k][1] 	= prev_a;
				steps[k][1] 		= components[k].reverse.pos;			
		}
		components[k].reverse.a 	= prev_a;
		components[k].bidir.w 	= prev_w_bidir;
		components[k].forward.w = prev_w_forward;
		components[k].reverse.w = prev_w_reverse;
		
	}
	
	for (int k =0; k < K;k++){
		components[k].forward.pos 	= steps[k][0];
		components[k].forward.b 	= new_bounds[k][0];
		components[k].reverse.pos 	= steps[k][1];
		components[k].reverse.a 	= new_bounds[k][1];	
	}
	update_weights_only(components, data, K, add);
	return base_ll;
			
}


//=========================================================
//For Classifier class / wrapper around EM

classifier::classifier(int k, double ct, int mi, double nm,
	double R_MU, double alpha_0, double beta_0,
	double alpha_1, double beta_1, double alpha_2,double alpha_3){
	K 						= k ;
	seed 					= true;
	convergence_threshold 	= ct;
	max_iterations 			= mi;
	noise_max 				= nm;
	p 						= 0.8;
	last_diff 				= 0;
	r_mu 					= R_MU;

	//=============================
	//hyperparameters
	ALPHA_0=alpha_0, BETA_0=beta_0, ALPHA_1=alpha_1, BETA_1=beta_1;
	ALPHA_2=alpha_2, ALPHA_3=alpha_3;

	move_l = true;

}
classifier::classifier(int k, double ct, int mi, double nm,
	double R_MU, double alpha_0, double beta_0,
	double alpha_1, double beta_1, double alpha_2,double alpha_3, bool MOVE){
	K 						= k ;
	seed 					= true;
	convergence_threshold 	= ct;
	max_iterations 			= mi;
	noise_max 				= nm;
	p 						= 0.8;
	last_diff 				= 0;
	r_mu 					= R_MU;

	//=============================
	//hyperparameters
	ALPHA_0=alpha_0, BETA_0=beta_0, ALPHA_1=alpha_1, BETA_1=beta_1;
	ALPHA_2=alpha_2, ALPHA_3=alpha_3;
	move_l 	= MOVE;
}
classifier::classifier(double ct, int mi, double nm,
	double R_MU, double alpha_0, double beta_0,
	double alpha_1, double beta_1, double alpha_2,double alpha_3, vector<vector<double>> IP){
	seed 					= true;
	convergence_threshold 	= ct;
	max_iterations 			= mi;
	noise_max 				= nm;
	p 						= 0.8;
	last_diff 				= 0;
	r_mu 					= R_MU;

	//=============================
	//hyperparameters
	ALPHA_0=alpha_0, BETA_0=beta_0, ALPHA_1=alpha_1, BETA_1=beta_1;
	ALPHA_2=alpha_2, ALPHA_3=alpha_3;
	init_parameters 		= IP;
	
}




classifier::classifier(){};//empty constructor

int classifier::fit(segment * data, vector<double> mu_seeds){
	//=========================================================================
	//for resets
	random_device rd;
	mt19937 mt(rd());
	uniform_real_distribution<double> dist_uni(data->minX,data->maxX);
	
	double l 	= data->maxX - data->minX;
	pi 	= sum(data->X[1], data->XN)/ data->N;
	double vl 	= 1.0 / l;
	if (K==0){
		//calc_likeihood coming from uniform model, only
		ll 	= 0;
		double SS 	= 0;
		for (int i = 0; i < data->XN; i ++){
			ll+=(LOG(vl*(pi) )*data->X[1][i]);
			ll+=(LOG(vl*(1-pi))*data->X[2][i]);
			SS+=data->X[1][i];
		}
		if (not isfinite(ll) or ll==nINF){
			printf("%f, %f, %f\n",SS ,data->minX, data->maxX );
		}
		converged=true;
		last_diff=0;
		components 	= new component[1];
		components[K].initialize(0., data, 0., 0. , noise_max, pi);
		return 1;
	}
	int add 	= noise_max>0;
       
	components 	= new component[K+add];

	//initialize components
	for (int k = 0; k < K; k++){
		components[k].set_priors(ALPHA_0, BETA_0, ALPHA_1, BETA_1, ALPHA_2, ALPHA_3);
	}

	//===========================================================================
	//random seeds, initialize
	int i 	= 0;
	double mu;
	
	for (int k = 0; k < K; k++){
		if (mu_seeds.size()>0){
			i 	= sample_centers(mu_seeds ,  p);
			mu 	= mu_seeds[i];
			if (r_mu > 0){
				normal_distribution<double> dist_r_mu(mu, r_mu);
				mu 		= dist_r_mu(mt);
			}
		}else{
			mu 			= dist_uni(mt);
		}
		components[k].initialize(mu, data, K, data->SCALE , 0., 0.);
		if (mu_seeds.size() > 0){
			mu_seeds.erase (mu_seeds.begin()+i);	
		}
	}
       
	if (add){
		components[K].initialize(0., data, 0., 0. , noise_max, pi);
	}
 		
	//===========================================================================
	int t 			= 0; //EM loop ticker
	double prevll 	= nINF; //previous iterations log likelihood
	converged 		= false; //has the EM converged?
	double norm_forward, norm_reverse,N; //helper variables
	while (t < max_iterations && not converged){
		
		//******
		//reset old sufficient statistics
		for (int k=0; k < K+add; k++){
			components[k].reset();
			if (components[k].EXIT){
				converged=false, ll=nINF;
				return 0;
			}
		       
		}
		
		//******
		//E-step
		for (int i =0; i < data->XN;i++){
			norm_forward=0;
			norm_reverse=0;
			for (int k=0; k < K+add; k++){
				if (data->X[1][i]){//if there is actually data point here...
					norm_forward+=components[k].evaluate(data->X[0][i],1);
				}
				if (data->X[2][i]){//if there is actually data point here...
					norm_reverse+=components[k].evaluate(data->X[0][i],-1);
				}
			}
			//now we need to add the sufficient statistics
			for (int k=0; k < K+add; k++){
				if (norm_forward){
					components[k].add_stats(data->X[0][i], data->X[1][i], 1, norm_forward);
				}
				if (norm_reverse){
					components[k].add_stats(data->X[0][i], data->X[2][i], -1, norm_reverse);
				}
			}

		
		}
		//******
		//M-step
		//get normalizing constant
		N=0;
		for (int k = 0; k < K+add; k++){
			N+=(components[k].get_all_repo());
		}
		//update the new parameters
		for (int k = 0; k < K+add; k++){
			components[k].update_parameters(N, K);
		}
		

		ll 	= calc_log_likelihood(components, K+add, data);
		//******
		//Move Uniform support		
		if (move_l){
			ll 	= move_uniforom_support(components, K, add, data,move, ll);
		}
		if (abs(ll-prevll)<convergence_threshold){
			converged=true;
		}
		if (not isfinite(ll)){
			ll 	= nINF;
			return 0;	
		}
		last_diff=abs(ll-prevll);

		prevll=ll;
		// for (int c = 0; c<K;c++){
		// 	components[c].print();
		// }

		t++;
	}
	
	return 1;
}
int classifier::fit_uniform_only(segment * data){
	//so in this case, there are a set of bidirectionals that are set and we are going to just try and maximize their 
	//elongation support first (and then maybe restimate parameters?)
	int K 		= init_parameters.size();
	components 	= new component[K+1];
	//printf("--BEFORE--\n");
	for (int k =0; k < K;k++){
		components[k].initialize_with_parameters(init_parameters[k], data, K);
	//	components[k].print();
	}
	update_weights_only(components, data, K, 1);
			
	vector<double> empty;
	components[K].initialize_with_parameters(empty, data, K);

	bool converged=false;
	int t=0;
	ll 	= calc_log_likelihood(components, K+1, data  );
	double prevll 	= nINF;
	int times 		= 0;
	int changes 	= 0;

	while (not converged and t < max_iterations){
		ll 	= move_uniforom_support2( components, K, 1, data, move,  ll);
		if (prevll == ll){
			times++;
		}else{
			times =0;
			changes++;
		}
		if (times> 200){
			// printf("--AFTER--\n");
			// printf("%d\n", changes);
			// for (int k =0; k < K;k++){
			// 	components[k].print();			
			// }
			converged=true;
		}
		prevll=ll;
		t++;
	}
	//now we can probably remaximize...but maybe save that for a later date...


	return 1;









}
string classifier::print_out_components(){
	string text 	= "";
	for (int k = 0; k < K; k++){
		text+=components[k].write_out();
	}
	return text;
}














