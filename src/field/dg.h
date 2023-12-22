#ifndef __DG_H
#define __DG_H

#define forEachLgl(i_,j_,k_)                                            \
            for(Int i_ = 0;i_ < NPX;i_++)                               \
                for(Int j_ = 0;j_ < NPY;j_++)                           \
                    for(Int k_ = 0;k_ < NPZ;k_++)
                    
#define forEachLglR(i_,j_,k_)                                           \
            for(Int i_ = NPX;i_-- > 0;)                                 \
                for(Int j_ = NPY;j_-- > 0;)                             \
                    for(Int k_ = NPZ;k_-- > 0;)

#define forEachLglXY(i_,j_)                             \
            for(Int i_ = 0;i_ < NPX;i_++)               \
                for(Int j_ = 0;j_ < NPY;j_++)
#define forEachLglXZ(i_,k_)                             \
            for(Int i_ = 0;i_ < NPX;i_++)               \
                for(Int k_ = 0;k_ < NPZ;k_++)
#define forEachLglYZ(j_,k_)                             \
            for(Int j_ = 0;j_ < NPY;j_++)               \
                for(Int k_ = 0;k_ < NPZ;k_++)           
                                                                    
#define forEachLglX(i_)                                 \
            for(Int i_ = 0;i_ < NPX;i_++)
#define forEachLglY(j_)                                 \
            for(Int j_ = 0;j_ < NPY;j_++)
#define forEachLglZ(k_)                                 \
            for(Int k_ = 0;k_ < NPZ;k_++)
                                                                                            
#define forEachLglBoundX(i_)                                            \
            for(Int i_ = 0;i_ < NPX;i_+=((NPX-1) ? (NPX-1) : 1))
#define forEachLglBoundY(j_)                                            \
            for(Int j_ = 0;j_ < NPY;j_+=((NPY-1) ? (NPY-1) : 1))
#define forEachLglBoundZ(k_)                                            \
            for(Int k_ = 0;k_ < NPZ;k_+=((NPZ-1) ? (NPZ-1) : 1))

#define isBoundary(i_,j_,k_)                            \
    (i_ == 0 || i_ == NPX - 1 ||                        \
     j_ == 0 || j_ == NPY - 1 ||                        \
     k_ == 0 || k_ == NPZ - 1)
                                                                
#define INDEX4(c_,i_,j_,k_) \
    ((c_) * NPX * NPY * NPZ + (i_) * NPY * NPZ + (j_) * NPZ + (k_))
    
#define INDEX3(i_,j_,k_) \
    ((i_) * NPY * NPZ + (j_) * NPZ + (k_))

#define INDEX_X(i_,j_,k_,m_) \
    (INDEX3(i_,j_,k_) * NPI + (m_))
                                    
#define INDEX_Y(i_,j_,k_,m_) \
    (INDEX3(i_,j_,k_) * NPI + (m_) + NPX)
                                    
#define INDEX_Z(i_,j_,k_,m_) \
    (INDEX3(i_,j_,k_) * NPI + (m_) + NPX + NPY)
                                                                        
#define INDEX_TX(i_,j_,k_,m_) \
    (INDEX3(m_,j_,k_) * NPI + (i_))
                                    
#define INDEX_TY(i_,j_,k_,m_) \
    (INDEX3(i_,m_,k_) * NPI + (j_) + NPX)
                                    
#define INDEX_TZ(i_,j_,k_,m_) \
    (INDEX3(i_,j_,m_) * NPI + (k_) + NPX + NPY)
    
#define DPSI_(dij,i,j,k,ii,jj,kk)   {                                                   \
    Scalar dpsi_ij_0 = dpsi[0][ii*NPX+i] *   psi[1][jj*NPY+j] *   psi[2][kk*NPZ+k];     \
    Scalar dpsi_ij_1 =  psi[0][ii*NPX+i] *  dpsi[1][jj*NPY+j] *   psi[2][kk*NPZ+k];     \
    Scalar dpsi_ij_2 =  psi[0][ii*NPX+i] *   psi[1][jj*NPY+j] *  dpsi[2][kk*NPZ+k];     \
    dij = Vector(dpsi_ij_0,dpsi_ij_1,dpsi_ij_2);                                        \
}

#define DPSI(dij,i,j,k)  DPSI_(dij,i,j,k,ii,jj,kk)
#define DPSIR(dij,i,j,k) DPSI_(dij,ii,jj,kk,i,j,k)
            
namespace DG {
    extern Scalar **psi; /**< Basis function values at LGL points */
    extern Scalar **dpsi; /**< Derivatives of basis functon values at LGL points */
    extern Scalar **xgl; /**< Quadrature points */
    extern Scalar **wgl; /**< Quarature weights */
    extern TensorCellField Jinv; /**< Jacobian matrix*/
    extern Int NPX, NPY, NPZ; /**< Number of LGL points in each direction */
    extern Scalar Penalty; /**< Factor for interior penalty method */
    extern Scalar** psiRef; /**< psi for refinement in a 2:1 situation */
    extern Scalar** psiCor; /**< psi for coarsening in a 2:1 situation */

    void legendre(int p, Scalar x,Scalar& L0,Scalar& L0_1,Scalar& L0_2);
    void legendre_gauss_lobatto(int N, Scalar* xgl, Scalar* wgl);
    void lagrange_basis(int N,Scalar* xgl,int Ns,Scalar* xs,Scalar* psi);
    void lagrange_basis_derivative(int N, Scalar* xgl, int Ns, Scalar* xs, Scalar* dpsi);
    void init_poly();
    void init_basis();
    void init_geom();

    /** Expand fields originally defined on elements to nodes*/
    template<class type, ENTITY entity>
    void expand(MeshField<type,entity>& cF) {
        if(DG::NPMAT) {
            Int block;
            if(entity == CELL) 
                block = NP;
            else if(entity == FACET) 
                block = NPF;

            for(int i = cF.size() - 1;i >= 0;i -= block) {
                Int ii = i / block;
                type C = cF[ii];
                for(Int j = 0; j < block;j++)
                    cF[i - j] = C; 
            }
        }
    }
}

#endif
