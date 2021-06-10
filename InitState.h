#ifndef __INITSTATE_H_CMC__
#define __INITSTATE_H_CMC__
#include "SystemStruct.h"

template <typename SiteType>
MPS get_non_inter_ground_state (const WireSystem& sys, const SiteType& sites, Real muL=0., Real muS=0., Real muR=0.)
{
    mycheck (length(sites) == sys.N(), "size not match");
    int Ns=0, Np=0;
    Real E = 0.;
    vector<string> state (sys.N()+1, "Emp");
    for(string p : {"L","S","R"})
    {
        auto const& chain = sys.parts().at(p);
        Real mu;
        if (p == "L")      mu = muL;
        else if (p == "R") mu = muR;
        else if (p == "S") mu = muS;
        for(int i = 1; i <= chain.L(); i++)
        {
            auto const& en = chain.ens()(i-1);
            int j = sys.to_glob (p, i);
            if (en-mu < 0.)
            {
                state.at(j) = "Occ";
                E += en;
                Np++;
                if (p == "S")
                    Ns++;
            }
        }
    }
    // Capacity site
    {
        int j = sys.to_glob ("C",1);
        state.at(j) = str(Ns);
    }

    InitState init (sites);
    for(int i = 1; i <= sys.N(); i++)
        init.set (i, state.at(i));

    cout << "orbitals, segment, ki, energy, state" << endl;
    for(int i = 1; i <= sys.orbs().size(); i++)
    {
        auto [seg, ki, en] = sys.orbs().at(i-1);
        cout << i << " " << seg << " " << ki << " " << en << " " << state.at(i) << endl;
    }
    cout << "initial energy = " << E << endl;
    cout << "initial particle number = " << Np << endl;
    return MPS (init);
}

tuple<MPS,MPS,Real,Real,Real,Real>
get_scatter_ground_state_SC
(const WireSystem& sys, Real mu, Real Delta,
 const Sweeps& sweeps, const Args& args)
{
    auto chain = sys.parts().at("S");
    SpecialFermion sites (chain.L(), {args,"special_qn",true});
    int L_offset = sys.parts().at("L").L()+1;
    AutoMPO ampo (sites);
    // Diagonal terms
    for(auto [coef, op, i] : chain.ops())
    {
        int j = sys.to_glob ("S",i)-L_offset;
        ampo += coef-mu, op, j;
    }
    // Superconducting
    string p = "S";
    for(int i = 1; i < chain.L(); i++)
    {
        auto terms = quadratic_term_coefs<Real> (chain, chain, i, i+1, false, false);
        for(auto [c12, k1, k2] : terms)
        {
            int j1 = sys.to_glob (p,k1) - L_offset;
            int j2 = sys.to_glob (p,k2) - L_offset;
            if (j1 != j2)
            {
                auto c = Delta * c12;
                auto cc = iutility::conjT (c);
                ampo += -c, "C", j1, "C", j2;
                ampo += -cc, "Cdag", j2, "Cdag", j1;
            }
        }
    }
    auto H0 = toMPO (ampo);

    // Solve the ground states in even and odd parities by DMRG
    InitState init (sites);
    auto psi0 = MPS (init);
    init.set(1,"Occ");
    auto psi1 = MPS (init);

    auto en0 = dmrg (psi0, H0, sweeps, {"Quiet",true});
    auto en1 = dmrg (psi1, H0, sweeps, {"Quiet",true});

    cout << setprecision(14);
    AutoMPO Nampo (sites);
    for(int i = 1; i <= length(sites); i++)
        Nampo += 1.0,"N",i;
    auto Nmpo = toMPO (Nampo);
    Real Np0 = inner (psi0,Nmpo,psi0),
         Np1 = inner (psi1,Nmpo,psi1);

    return {psi0, psi1, en0, en1, Np0, Np1};
}

template <typename SiteType, typename Para>
MPS get_ground_state_SC (const WireSystem& sys, const SiteType& sites,
                         Real muL, Real muS, Real muR, const Para& para,
                         const Sweeps& sweeps, const Args& args)
{
    mycheck (length(sites) == sys.N(), "size not match");
    // Leads
    Real E_lead = 0.;
    int Np_lead = 0;
    vector<string> state (sys.N()+1, "Emp");
    for(string p : {"L","R"})
    {
        auto const& chain = sys.parts().at(p);
        Real mu = (p == "L" ? muL : muR);
        for(int i = 1; i <= chain.L(); i++)
        {
            auto const& en = chain.ens()(i-1);
            int j = sys.to_glob (p, i);
            if (en-mu < 0.)
            {
                state.at(j) = "Occ";
                E_lead += en-mu;
                Np_lead++;
            }
        }
    }
    cout << "lead E = " << E_lead << endl;
    cout << "lead Np = " << Np_lead << endl;

    // Get ground state of scatter
    auto [psi0, psi1, enSC0, enSC1, Np0, Np1] = get_scatter_ground_state_SC (sys, muS, para.Delta, sweeps, args);

    // Capacity site
    // Find out the charge numbers, as integers, that lowest the charging energy, for even and odd parities
    int maxOcc = args.getInt("MaxOcc");
    Real enC0 = std::numeric_limits<double>::max(),
         enC1 = std::numeric_limits<double>::max();
    vector<int> ns (1,0);
    for(int n = 1; n <= maxOcc; n++)
    {
        ns.push_back (n);
        ns.push_back (-n);
    }
    int n_even, n_odd;
    for(int n : ns)
    {
        Real nn = n - para.Ng;
        Real enC = para.Ec * nn*nn;
        if (n % 2 == 0 and enC < enC0)
        {
            enC0 = enC;
            n_even = n;
        }
        if (n % 2 == 1 and enC < enC1)
        {
            enC1 = enC;
            n_odd = n;
        }
    }
    // Combine the scatter energies to decide to choose even or odd sector
    Real en0 = enC0 + enSC0,
         en1 = enC1 + enSC1;
    Real en   = (en0 < en1 ? en0 : en1);
    int  n    = (en0 < en1 ? n_even : n_odd);
    Real Np   = (en0 < en1 ? Np0 : Np1);
    auto psiS = (en0 < en1 ? psi0 : psi1);
    // Set state
    int ic = sys.to_glob ("C",1);
    state.at(ic) = str(n);
    // Print
    cout << "Init scatter Np (even,odd) = (" << Np0 << "," << Np1 << ") -> " << Np << endl;
    cout << "Init scatter E (even,odd) = (" << en0 << "," << en1 << ") -> " << en << endl
         <<  "\tE_C = (" << enC0 << "," << enC1 << ")" << endl
         <<  "\tE_SC, gap = (" << enSC0 << "," << enSC1 << "), " << abs(enSC0-enSC1) << endl;
    cout << "Init scatter total charge = (" << n_even << "," << n_odd << ") -> " << n << endl;

    // Initialize the leads and the charge site
    InitState init (sites);
    for(int i = 1; i <= sys.N(); i++)
        init.set (i, state.at(i));
    auto psi = MPS (init);

    // Replace the tensors in the scatter
    int L_offset = sys.parts().at("L").L()+1;
    for(int i = 1; i <= length(psiS); i++)
    {
        int i0 = i+L_offset;
        auto iis = findIndex (psi(i0), "Site");
        auto iis2 = findIndex (psiS(i), "Site");
        Index iil;
        if (i == 1)
            iil = leftLinkIndex (psi, i0);
        else if (i == length(psiS))
            iil = rightLinkIndex (psi, i0);
        psiS.ref(i).replaceInds ({iis2}, {iis});
        psi.ref(i0) = psiS(i);
        if (iil)
        {
            mycheck (dim(iil) == 1, "not dummy index");
            psi.ref(i0) *= setElt(iil=1);
        }
        state.at(i0) = "*";
    }
    psi.position(1);
    psi.normalize();

    cout << "orbitals, segment, ki, energy, state" << endl;
    for(int i = 1; i <= sys.orbs().size(); i++)
    {
        auto [seg, ki, en] = sys.orbs().at(i-1);
        cout << i << " " << seg << " " << ki << " " << en << " " << state.at(i) << endl;
    }
    return psi;
}

#endif
