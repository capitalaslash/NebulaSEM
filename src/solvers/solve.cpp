#include "solve.h"

/* *********************************************************************
 *  Solve system of linear equations iteratively
 * *********************************************************************/
template<class type, ENTITY entity>
Scalar getResidual(const MeshField<type,entity>& r,
				   const MeshField<type,entity>& cF,
				   bool sync) {
	type res[2];
	res[0] = type(0);
	res[1] = type(0); 
	for(Int i = 0;i < Mesh::gBCSfield;i++) {
		res[0] += (r[i] * r[i]);
		res[1] += (cF[i] * cF[i]);
	}
	if(sync) {
		type global_res[2];
		MP::allsum(res,global_res,2);
		res[0] = global_res[0];
		res[1] = global_res[1];
	}
	return sqrt(sdiv(mag(res[0]), mag(res[1])));
}

template<class type>
void SolveT(const MeshMatrix<type>& M) {
	using namespace Mesh;
	using namespace DG;
	MeshField<type,CELL> r,p,AP;
	MeshField<type,CELL> r1(false),p1(false),AP1(false);   
	MeshField<type,CELL>& cF = *M.cF;
	MeshField<type,CELL>& buffer = AP;
	ScalarCellField D = M.ap,iD = (1.0 / M.ap);
	Scalar res,ires;
	type alpha,beta,o_rr = type(0),oo_rr;
	Int iterations = 0;
	bool converged = false;

	/****************************
	 * Parallel controls
	 ***************************/
	bool print = (MP::host_id == 0 && Controls::print);
	int  end_count = 0;
	bool sync = (Controls::parallel_method == Controls::BLOCKED)
		&& gInterMesh.size();
	std::vector<bool> sent_end(gInterMesh.size(),false);

	/****************************
	 * Identify solver type
	 ***************************/
	if(print) {
		if(M.flags & M.SYMMETRIC)
			MP::printH("SYMM-");
		else
			MP::printH("ASYM-");
		if(Controls::Solver == Controls::JACOBI)
			MP::print("JAC :");
		else if(Controls::Solver == Controls::SOR)
			MP::print("SOR :");
		else {
			switch(Controls::Preconditioner) {
			case Controls::NOPR: MP::print("NONE-PCG :"); break;
			case Controls::DIAG: MP::print("DIAG-PCG :"); break;
			case Controls::SSOR: MP::print("SSOR-PCG :"); break;
			case Controls::DILU: MP::print("DILU-PCG :"); break;
			}
		}
	}
	/****************************
	 * Jacobi sweep
	 ***************************/
#define JacobiSweep() {								\
	cF = iD * getRHS(M,sync);						\
}
	/****************************
	 *  Forward/backward GS sweeps
	 ****************************/
#define Sweep_(X,B,ii) {							\
	Cell& c = gCells[ii];							\
	for(Int j = 0;j < NP;j++) {						\
		Int i = ii * NP + j;						\
		type ncF = B[i];							\
		if(NPMAT) {									\
			type val(Scalar(0));					\
			for(Int k = 0;k < NP;k++)				\
				val += X[ii * NP + k] * 			\
				   M.adg[ii * NPMAT + j * NP + k];	\
			ncF += val;								\
		}											\
		forEach(c,j) {								\
			Int faceid = c[j];						\
 			for(Int n = 0; n < NPF;n++) {			\
				Int k = faceid * NPF + n;			\
				Int c1 = gFO[k];					\
				Int c2 = gFN[k];					\
				if(i == c1)							\
					ncF += X[c2] * M.an[1][k];		\
				else if(i == c2)					\
					ncF += X[c1] * M.an[0][k];		\
			}										\
		}											\
		ncF *= iD[i];								\
		X[i] = X[i] * (1 - Controls::SOR_omega) +	\
			ncF * (Controls::SOR_omega);			\
	}												\
}
#define ForwardSweep(X,B) {							\
	ASYNC_COMM<type> comm(&X[0]);					\
	comm.send();									\
	for(Int ii = 0;ii < gBCSI;ii++)					\
		Sweep_(X,B,ii);								\
	comm.recv();									\
	for(Int ii = gBCSI;ii < gBCS;ii++)				\
		Sweep_(X,B,ii);								\
}
	/***********************************
	 *  Forward/backward substitution
	 ***********************************/
#define Substitute_(X,B,ii,forw,tr) {				\
	Cell& c = gCells[ii];							\
	for(Int j = 0;j < NP;j++) {						\
		Int i = ii * NP + j;						\
		type ncF = B[i];							\
		if(NPMAT) {									\
			type val(Scalar(0));					\
			for(Int k = 0;k < NP;k++) {				\
				if((forw && (k < j)) ||				\
				  (!forw && (j < k))) {				\
				  	Int ind;						\
				  	if(tr) ind = k * NP + j;		\
				  	else   ind = j * NP + k;		\
					val += X[ii * NP + k] * 		\
				   		M.adg[ii * NPMAT + ind];	\
				}									\
			}										\
			ncF += val;								\
		}											\
		forEach(c,ci) {								\
			Int faceid = c[ci];						\
 			for(Int n = 0; n < NPF;n++) {			\
				Int k = faceid * NPF + n;			\
				Int c1 = gFO[k];					\
				Int c2 = gFN[k];					\
				if(i == c1) {						\
					if((forw && (c2 < c1)) ||		\
					  (!forw && (c1 < c2)))	{		\
					ncF += X[c2] * M.an[1 - tr][k];	\
					}								\
				} else if(i == c2) {				\
					if((forw && (c2 > c1)) ||		\
					  (!forw && (c1 > c2)))			\
					ncF += X[c1] * M.an[0 + tr][k];	\
				}									\
			}										\
		}											\
		ncF *= iD[i];								\
		X[i] = ncF;									\
	}												\
}
#define ForwardSub(X,B,TR) {						\
	for(Int ii = 0;ii < gBCS;ii++)					\
		Substitute_(X,B,ii,true,TR);				\
}
#define BackwardSub(X,B,TR) {						\
	for(int ii = gBCS;ii >= 0;ii--)					\
		Substitute_(X,B,ii,false,TR);				\
}
#define DiagSub(X,B) {								\
	for(Int i = 0;i < gBCSfield;i++)				\
		X[i] = B[i] * iD[i];						\
}
	/***********************************
	 *  Preconditioners
	 ***********************************/
#define precondition_(R,Z,TR) {						\
	using namespace Controls;						\
	if(Preconditioner == Controls::NOPR) {			\
		Z = R;										\
	} else if(Preconditioner == Controls::DIAG) {	\
		DiagSub(Z,R);								\
	} else {										\
		if(Controls::Solver == Controls::PCG) {		\
			ForwardSub(Z,R,TR);						\
			Z = Z * D;								\
			BackwardSub(Z,Z,TR);					\
		}											\
	}												\
}
#define precondition(R,Z) precondition_(R,Z,0)
#define preconditionT(R,Z) precondition_(R,Z,1)
	/***********************************
	 *  SAXPY and DOT operations
	 ***********************************/
#define Taxpy(Y,I,X,alpha_) {						\
	for(Int i = 0;i < gBCSfield;i++)				\
		Y[i] = I[i] + X[i] * alpha_;				\
}
#define Tdot(X,Y,sum) {								\
	sum = type(0);									\
	for(Int i = 0;i < gBCSfield;i++)				\
		sum += X[i] * Y[i];							\
}
	/***********************************
	 *  Synchronized sum
	 ***********************************/
#define REDUCE(typ,var)	if(sync) {					\
	typ t;											\
	MP::allsum(&var,&t,1);							\
	var = t;										\
}
	/***********************************
	 *  Residual
	 ***********************************/
#define CALC_RESID() {								\
	r = M.Su - mul(M,cF);							\
	forEachS(r,k,gBCSfield)							\
		r[k] = type(0);								\
	precondition(r,AP);								\
	forEachS(AP,k,gBCSfield)						\
		AP[k] = type(0);							\
	res = getResidual(AP,cF,sync);					\
	if(Controls::Solver == Controls::PCG) {			\
		Tdot(r,AP,o_rr);							\
		REDUCE(type,o_rr);							\
		p = AP;										\
		if(!(M.flags & M.SYMMETRIC)) {				\
			r1 = r;									\
			p1 = p;									\
		}											\
	}												\
}
	/****************************
	 * Initialization
	 ***************************/
	if(Controls::Solver == Controls::PCG) {
		if(!(M.flags & M.SYMMETRIC)) {
			/* Allocate BiCG vars*/
			r1.allocate();
			p1.allocate();
			AP1.allocate();
		} else {
			if(Controls::Preconditioner == Controls::SSOR) {
				/*SSOR pre-conditioner*/
				iD *= Controls::SOR_omega;
				D *=  (2.0 / Controls::SOR_omega - 1.0);	
			} else if(Controls::Preconditioner == Controls::DILU) {
				/*D-ILU(0) pre-conditioner*/
				for(Int ii = 0;ii < gBCS;ii++) {
					Cell& c = gCells[ii];
					for(Int j = 0;j < NP;j++) {	
						Int i = ii * NP + j;
						if(NPMAT) {
							Scalar val = 0.0;
							for(Int k = 0;k < NP;k++)
								val += M.adg[ii * NPMAT + j * NP + k] *
								       M.adg[ii * NPMAT + k * NP + j] *
								       iD[ii * NP + k];
							D[i] -= val;
						}	
						forEach(c,ci) {								
							Int faceid = c[ci];
							for(Int n = 0; n < NPF;n++) {
								Int k = faceid * NPF + n;							
								Int c1 = gFO[k];						
								Int c2 = gFN[k];						
								if(i == c1) {
									if(c2 > c1) D[c2] -= 
									(M.an[0][k] * M.an[1][k] * iD[c1]);	
								} else if(i == c2) {
									if(c1 > c2) D[c1] -= 
									(M.an[0][k] * M.an[1][k] * iD[c2]);		
								}		
							}								
						}
					}			
				}
				iD = (1.0 / D);
			}
			/*end*/
		}
	}
	/***********************
	 *  Initialize residual
	 ***********************/
	CALC_RESID();
	ires = res;
	/********************************************************
	* Initialize exchange of ghost cells just once.
	* Lower numbered processors send message to higher ones.
	*********************************************************/  
	if(!sync) {
		end_count = gInterMesh.size();
		forEach(gInterMesh,i) {
			interBoundary& b = gInterMesh[i];
			if(b.from < b.to) {
				IntVector& f = *(b.f);
				Int buf_size = f.size() * NPF;
				/*send*/
				forEach(f,j) {
					Int faceid = f[j];
					Int offset = j * NPF;
					for(Int n = 0; n < NPF;n++) {
						Int k = faceid * NPF + n;
						buffer[offset + n] = cF[gFO[k]];
					}
				}
				MP::send(&buffer[0],buf_size,b.to,MP::FIELD);
			}
		}
	}
    /* **************************
	 * Iterative solution
	 * *************************/
	while(iterations < Controls::max_iterations) {
		/*counter*/
		iterations++;

		/*select solver*/
		if(Controls::Solver != Controls::PCG) {
			p = cF;
			/*Jacobi and SOR solvers*/
			if(Controls::Solver == Controls::JACOBI) {
				JacobiSweep();
			} else {
				ForwardSweep(cF,M.Su);
			}
			/*residual*/
			for(Int i = 0;i < gBCSfield;i++)
				AP[i] = cF[i] - p[i];
		} else if(M.flags & M.SYMMETRIC) {
			/*conjugate gradient*/
			AP = mul(M,p,sync);
			Tdot(p,AP,oo_rr);
			REDUCE(type,oo_rr);
			alpha = sdiv(o_rr , oo_rr);
			Taxpy(cF,cF,p,alpha);
			Taxpy(r,r,AP,-alpha);
			precondition(r,AP);
			oo_rr = o_rr;
			Tdot(r,AP,o_rr);
			REDUCE(type,o_rr);
			beta = sdiv(o_rr , oo_rr);
			Taxpy(p,AP,p,beta);
			/*end*/
		} else {
			/* biconjugate gradient*/
			AP = mul(M,p,sync);
			AP1 = mult(M,p1,sync);
			Tdot(p1,AP,oo_rr);
			REDUCE(type,oo_rr);
			alpha = sdiv(o_rr , oo_rr);
			Taxpy(cF,cF,p,alpha);
			Taxpy(r,r,AP,-alpha);
			Taxpy(r1,r1,AP1,-alpha);
			precondition(r,AP);
			preconditionT(r1,AP1);
			oo_rr = o_rr;
			Tdot(r1,AP,o_rr);
			REDUCE(type,o_rr);
			beta = sdiv(o_rr , oo_rr);
			Taxpy(p,AP,p,beta);
			Taxpy(p1,AP1,p1,beta);
			/*end*/
		}
		/* *********************************************
		* calculate norm of residual & check convergence
		* **********************************************/
		res = getResidual(AP,cF,sync);
		if(res <= Controls::tolerance
			|| iterations == Controls::max_iterations)
			converged = true;
PROBE:
		/* **********************************************************
		 * Update ghost cell values. Communication is NOT forced on 
		 * every iteration,rather a non-blocking probe is used to 
		 * process messages as they arrive.
		 ************************************************************/
		if(!sync) {
			int source,message_id;
			/*probe*/
			while(MP::iprobe(source,message_id,MP::FIELD)
			   || MP::iprobe(source,message_id,MP::END)) {
				/*find the boundary*/
				Int patchi;
				for(patchi = 0;patchi < gInterMesh.size();patchi++) {
					if(gInterMesh[patchi].to == (Int)source) 
						break;
				}
				/*parse message*/
				if(message_id == MP::FIELD) {
					interBoundary& b = gInterMesh[patchi];
					IntVector& f = *(b.f);
					Int buf_size = f.size() * NPF;
					
					/*recieve*/
					MP::recieve(&buffer[0],buf_size,source,message_id);
					forEach(f,j) {
						Int faceid = f[j];
						Int offset = j * NPF;
						for(Int n = 0; n < NPF;n++) {
							Int k = faceid * NPF + n;
							cF[gFN[k]] = buffer[offset + n];
						}
					}
					
					/*Re-calculate residual.*/					
					CALC_RESID();
					if(res > Controls::tolerance
						&& iterations < Controls::max_iterations)
						converged = false;
					/* For communication to continue, processor have to send back 
					 * something for every message recieved.*/
					if(converged) {
						/*send END marker*/
						if(!sent_end[patchi]) {
							MP::send(source,MP::END);
							sent_end[patchi] = true;
						}
						continue;
					}
					
					/*send*/
					forEach(f,j) {
						Int faceid = f[j];
						Int offset = j * NPF;
						for(Int n = 0; n < NPF;n++) {
							Int k = faceid * NPF + n;
							buffer[offset + n] = cF[gFO[k]];
						}
					}
					MP::send(&buffer[0],buf_size,source,message_id);
					
				} else if(message_id == MP::END) {
					/*END marker recieved*/
					MP::recieve(source,message_id);
					end_count--;
					if(!sent_end[patchi]) {
						MP::send(source,MP::END);
						sent_end[patchi] = true;
					}
				}
			}
		}
		/* *****************************************
		* Wait untill all partner processors send us
		* an END message i.e. until end_count = 0.
		* *****************************************/
		if(converged) {
			if(end_count > 0) goto PROBE;
			else break;
		}
		/********
		 * end
		 ********/
	}

	/*solver info*/
	if(print)
		MP::print("Iterations %d Initial Residual "
		"%.5e Final Residual %.5e\n",iterations,ires,res);
}
template<class type>
void SolveTexplicit(const MeshMatrix<type>& M) {
	if(MP::host_id == 0 && Controls::print) {
		MP::printH("DIAG-DIAG:");
		MP::print("Iterations %d Initial Residual "
		"%.5e Final Residual %.5e\n",1,0.0,0.0);
	}
	*M.cF = M.Su / M.ap;
}
/***************************
 * Explicit instantiations
 ***************************/
#define SOLVE() {							\
	applyImplicitBCs(A);					\
	if(A.flags & A.DIAGONAL)				\
		SolveTexplicit(A);					\
	else									\
		SolveT(A);							\
	applyExplicitBCs(*A.cF,true,false);		\
}
void Solve(const MeshMatrix<Scalar>& A) {
	SOLVE();
}
void Solve(const MeshMatrix<Vector>& A) {
	SOLVE();
}
void Solve(const MeshMatrix<STensor>& A) {
	SOLVE();
}
void Solve(const MeshMatrix<Tensor>& A) {
	SOLVE();
}
#undef SOLVE
/* ********************
 *        End
 * ********************/
