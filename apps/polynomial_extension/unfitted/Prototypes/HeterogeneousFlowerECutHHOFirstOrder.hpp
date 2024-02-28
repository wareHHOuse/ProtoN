
#ifndef HeterogeneousFlowerECutHHOFirstOrder_hpp
#define HeterogeneousFlowerECutHHOFirstOrder_hpp

void HeterogeneousFlowerECutHHOFirstOrder(int argc, char **argv);


void HeterogeneousFlowerECutHHOFirstOrder(int argc, char **argv){
    
    bool report_energy_Q = false;
    
    size_t degree           = 0;
    size_t l_divs          = 0;
    size_t nt_divs       = 0;
    size_t int_refsteps     = 4;
    bool dump_debug         = false;

    int ch;
    while ( (ch = getopt(argc, argv, "k:l:r:n:d")) != -1 )
    {
        switch(ch)
        {
            case 'k':
                degree = atoi(optarg);
                break;

            case 'l':
                l_divs = atoi(optarg);
                break;

            case 'r':
                int_refsteps = atoi(optarg);
                break;
                
            case 'n':
                nt_divs = atoi(optarg);
                break;

            case 'd':
                dump_debug = true;
                break;

            case '?':
            default:
                std::cout << "wrong arguments" << std::endl;
                exit(1);
        }
    }

    argc -= optind;
    argv += optind;

    RealType r_c = 1.0;
    RealType a_c = 0.0;
    RealType b_c = 0.0;
    RealType c_c = 0.2;
    size_t n_c = 8;
    auto level_set_function = flower_level_set<RealType>(r_c, a_c, b_c, n_c, c_c);
    mesh_type msh = SquareGar6moreCutMesh(level_set_function, l_divs, int_refsteps);
    
    if (dump_debug)
    {
        dump_mesh(msh);
        output_mesh_info(msh, level_set_function);
        hho_degree_info hdi(degree+1, degree);
        PrintIntegrationRule(msh,hdi);
    }

    // Time controls : Final time value 1.0
    size_t nt = 10;
    for (unsigned int i = 0; i < nt_divs; i++) {
        nt *= 2;
    }
    RealType ti = 0.0;
    RealType tf = 1.0;
    RealType dt = (tf-ti)/nt;
    RealType t = ti;
    
    timecounter tc;
    
    // ERK(s) schemes
    int s = 4;
    Matrix<RealType, Dynamic, Dynamic> a;
    Matrix<RealType, Dynamic, 1> b;
    Matrix<RealType, Dynamic, 1> c;
    erk_butcher_tableau::erk_tables(s, a, b, c);
    hho_degree_info hdi(degree+1, degree);
    
    SparseMatrix<RealType> Kg, Mg;
    auto test_case = make_test_case_laplacian_waves_scatter(t,msh, level_set_function);
    test_case.parms.kappa_1 = 1.0; // rho_1 = kappa_1
    test_case.parms.kappa_2 = 1.0; // rho_2 = kappa_2
    test_case.parms.c_1 = std::sqrt(1.0);//std::sqrt(9.0);
    test_case.parms.c_2 = std::sqrt(1.0);//std::sqrt(3.0);
    auto method = make_gradrec_mixed_interface_method(msh, 1.0, test_case);
    size_t n_faces = 0;
    std::vector<std::pair<size_t,size_t>> cell_basis_data = create_mixed_kg_and_mg_cuthho_interface(msh, hdi, method, test_case, Kg, Mg, true, &n_faces);
    
    tc.tic();
    size_t n_face_dof, n_face_basis;
    size_t n_dof = Kg.rows();
    size_t n_cell_dof = 0;
    for (auto &chunk : cell_basis_data) {
        n_cell_dof += chunk.second;
    }
    n_face_dof = n_dof - n_cell_dof;
    n_face_basis = face_basis<mesh_type,RealType>::size(degree);
    
    Matrix<RealType, Dynamic, 1> x_dof, rhs = Matrix<RealType, Dynamic, 1>::Zero(n_dof, 1);
    erk_hho_scheme<RealType> analysis(Kg,rhs,Mg,n_face_dof);
    analysis.Kcc_inverse_irregular_blocks(cell_basis_data);
    analysis.Sff_inverse(std::make_pair(n_faces, n_face_basis));
    tc.toc();
    std::cout << bold << cyan << "ERK analysis created: " << tc << " seconds" << reset << std::endl;

    std::ofstream enery_file("e_two_fields_energy.txt");
    
    std::ofstream sensor_1_log("s1_cut_acoustic_e_two_fields.csv");
    std::ofstream sensor_2_log("s2_cut_acoustic_e_two_fields.csv");
    std::ofstream sensor_3_log("s3_cut_acoustic_e_two_fields.csv");
    
    typename mesh_type::point_type s1_pt(1.0/3.0, 1.0/3.0);
    typename mesh_type::point_type s2_pt(1.0/3.0, 2.0/3.0);
    typename mesh_type::point_type s3_pt(1.2, 1.0);
    std::pair<typename mesh_type::point_type,size_t> s1_pt_cell = std::make_pair(s1_pt, -1);
    std::pair<typename mesh_type::point_type,size_t> s2_pt_cell = std::make_pair(s2_pt, -1);
    std::pair<typename mesh_type::point_type,size_t> s3_pt_cell = std::make_pair(s3_pt, -1);
    
    for(size_t it = 1; it <= nt; it++){ // for each time step
        
        std::cout << std::endl;
        std::cout << "Time step number: " <<  it << std::endl;
        RealType t = dt*it+ti;
        auto test_case = make_test_case_laplacian_waves_scatter(t,msh, level_set_function);
        auto method = make_gradrec_mixed_interface_method(msh, 1.0, test_case);
        
        tc.tic();
        erk_step_cuthho_interface_scatter(it, s, ti, dt, a, b, c, msh, hdi, method, test_case, x_dof, analysis, sensor_1_log, sensor_2_log, sensor_3_log, s1_pt_cell, s2_pt_cell, s3_pt_cell);
        tc.toc();
        std::cout << bold << yellow << "ERK step performed in : " << tc << " seconds" << reset << std::endl;
        
        // energy evaluation
         if(report_energy_Q){

             RealType energy_0 = 0.125;
             Matrix<RealType, Dynamic, 1> cell_mass_tested = Mg * x_dof;
             Matrix<RealType, 1, 1> term_1 = x_dof.transpose() * cell_mass_tested;
             RealType energy_h = 0.5*term_1(0,0);

             energy_h /= energy_0;
             std::cout << bold << yellow << "Energy = " << energy_h << reset << std::endl;
             enery_file << std::setprecision(16) << t << " " << energy_h << std::endl;
         }
        
    }
    std::cout << "Number of equations : " << analysis.n_equations() << std::endl;
    std::cout << "Number of steps : " <<  nt << std::endl;
    std::cout << "Time step size : " <<  dt << std::endl;
    
}

#endif
