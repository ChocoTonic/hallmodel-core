// parity_runner — execute every case in contract/cases.json against the
// hallmodel-core kernel and write outputs/<id>.json in the schema the upstream
// contract/compare.py expects.
//
// This program is the C++ analogue of bw-chocotonic's bw_cpp/__init__.py: it
// performs the same default-fill, sex-encoding, and transpose-to-kernel-layout
// orchestration that the Python shim does, then dispatches into bw::Adult,
// bw::Child, and bw::energy directly. Built against the same kernel with the
// same -ffp-contract=off flag, its outputs match the upstream golden under
// the contract's tolerance (rtol=1e-9, atol=1e-12).
//
// Usage: parity_runner <cases.json> <output_dir>

#include <bw/adult.hpp>
#include <bw/child.hpp>
#include <bw/energy.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------- argument extraction ---------------------------------------------

static std::vector<double> as_vec(const json& j, std::size_t n_default) {
    std::vector<double> out;
    if (j.is_array()) {
        for (const auto& x : j) out.push_back(x.get<double>());
    } else {
        out.push_back(j.get<double>());
    }
    if (out.size() == 1 && n_default > 1) out.assign(n_default, out.front());
    return out;
}

static std::vector<double> sex_as_num(const json& j) {
    auto encode = [](const json& s) -> double {
        if (s.is_string()) {
            std::string v = s.get<std::string>();
            if (v == "male")   return 0.0;
            if (v == "female") return 1.0;
            throw std::invalid_argument("invalid sex: " + v);
        }
        return s.get<double>();
    };
    std::vector<double> out;
    if (j.is_array()) for (const auto& s : j) out.push_back(encode(s));
    else              out.push_back(encode(j));
    return out;
}

static bw::Matrix as_matrix(const json& j) {
    // Accepts a 2D nested array. Rows = outer, cols = inner.
    std::size_t nrow = j.size();
    std::size_t ncol = nrow ? j.at(0).size() : 0;
    bw::Matrix m(nrow, ncol);
    for (std::size_t i = 0; i < nrow; ++i)
        for (std::size_t k = 0; k < ncol; ++k)
            m(i, k) = j.at(i).at(k).get<double>();
    return m;
}

// Transpose a (n, ncol) matrix into a (ncol, n) one — matches bw_cpp shim's
// `.T` step before passing EIchange/NAchange into the kernel.
static bw::Matrix transpose(const bw::Matrix& m) {
    bw::Matrix t(m.ncol, m.nrow);
    for (std::size_t i = 0; i < m.nrow; ++i)
        for (std::size_t k = 0; k < m.ncol; ++k)
            t(k, i) = m(i, k);
    return t;
}

// ---------- JSON serialization ----------------------------------------------

static json vec_to_json(const std::vector<double>& v) {
    return json(v);
}

static json mat_to_json(const bw::Matrix& m) {
    json out = json::array();
    for (std::size_t i = 0; i < m.nrow; ++i) {
        json row = json::array();
        for (std::size_t k = 0; k < m.ncol; ++k) row.push_back(m(i, k));
        out.push_back(row);
    }
    return out;
}

static json strmat_to_json(const bw::StringMatrix& m) {
    json out = json::array();
    for (std::size_t i = 0; i < m.nrow; ++i) {
        json row = json::array();
        for (std::size_t k = 0; k < m.ncol; ++k) row.push_back(m(i, k));
        out.push_back(row);
    }
    return out;
}

// Child kernel emits per-individual arrays laid out data[i + nind*j]; reshape
// to a 2D (nind, nsteps) JSON nested list.
static json child_to_json(const std::vector<double>& data,
                          std::size_t nind, std::size_t nsteps) {
    json out = json::array();
    for (std::size_t i = 0; i < nind; ++i) {
        json row = json::array();
        for (std::size_t j = 0; j < nsteps; ++j) row.push_back(data[i + nind * j]);
        out.push_back(row);
    }
    return out;
}

// ---------- per-fn dispatchers ----------------------------------------------

static json run_adult(const json& args) {
    auto getarr = [&](const char* k) -> const json* {
        auto it = args.find(k);
        return it == args.end() ? nullptr : &(*it);
    };
    std::size_t n = 1;
    {
        auto* bwj = getarr("bw");
        if (bwj && bwj->is_array()) n = bwj->size();
    }

    std::vector<double> bw_v  = as_vec(*getarr("bw"),  n);
    std::vector<double> ht_v  = as_vec(*getarr("ht"),  n);
    std::vector<double> age_v = as_vec(*getarr("age"), n);
    std::vector<double> sex_v = sex_as_num(*getarr("sex"));
    while (sex_v.size() < n) sex_v.push_back(sex_v.back());

    auto* PALj  = getarr("PAL");
    auto* pcbj  = getarr("pcarb_base");
    auto* pcj   = getarr("pcarb");
    double PAL_default = 1.5, pcb_default = 0.5;
    std::vector<double> PAL_v = PALj ? as_vec(*PALj, n) : std::vector<double>(n, PAL_default);
    std::vector<double> pcb_v = pcbj ? as_vec(*pcbj, n) : std::vector<double>(n, pcb_default);
    std::vector<double> pc_v  = pcj  ? as_vec(*pcj,  n) : pcb_v; // pcarb defaults to copy of pcarb_base

    double days = args.value("days", 365.0);
    double dt   = args.value("dt", 1.0);
    bool check  = args.value("check_values", true);

    std::size_t ncol = static_cast<std::size_t>(std::llabs(static_cast<long long>(std::ceil(days / dt))));

    bw::Matrix EIc, NAc;
    if (auto* j = getarr("EIchange")) EIc = transpose(as_matrix(*j));
    else                              EIc = bw::Matrix(ncol, n, 0.0); // already in (day, indiv) layout
    if (auto* j = getarr("NAchange")) NAc = transpose(as_matrix(*j));
    else                              NAc = bw::Matrix(ncol, n, 0.0);

    bool have_EI  = getarr("EI")  != nullptr;
    bool have_fat = getarr("fat") != nullptr;

    double cd = std::ceil(days);
    bw::AdultResult res;
    if (!have_EI && !have_fat) {
        bw::Adult P(bw_v, ht_v, age_v, sex_v, EIc, NAc, PAL_v, pc_v, pcb_v, dt, check);
        res = P.rk4(cd);
    } else if (have_EI && !have_fat) {
        std::vector<double> EI_v = as_vec(*getarr("EI"), n);
        bw::Adult P(bw_v, ht_v, age_v, sex_v, EIc, NAc, PAL_v, pc_v, pcb_v, dt, EI_v, check, /*isEnergy=*/true);
        res = P.rk4(cd);
    } else if (!have_EI && have_fat) {
        std::vector<double> fat_v = as_vec(*getarr("fat"), n);
        bw::Adult P(bw_v, ht_v, age_v, sex_v, EIc, NAc, PAL_v, pc_v, pcb_v, dt, fat_v, check, /*isEnergy=*/false);
        res = P.rk4(cd);
    } else {
        std::vector<double> EI_v  = as_vec(*getarr("EI"),  n);
        std::vector<double> fat_v = as_vec(*getarr("fat"), n);
        bw::Adult P(bw_v, ht_v, age_v, sex_v, EIc, NAc, PAL_v, pc_v, pcb_v, dt, EI_v, fat_v, check);
        res = P.rk4(cd);
    }

    json out;
    out["Time"]                   = vec_to_json(res.Time);
    out["Age"]                    = mat_to_json(res.Age);
    out["Adaptive_Thermogenesis"] = mat_to_json(res.Adaptive_Thermogenesis);
    out["Extracellular_Fluid"]    = mat_to_json(res.Extracellular_Fluid);
    out["Glycogen"]               = mat_to_json(res.Glycogen);
    out["Fat_Mass"]               = mat_to_json(res.Fat_Mass);
    out["Lean_Mass"]              = mat_to_json(res.Lean_Mass);
    out["Body_Weight"]            = mat_to_json(res.Body_Weight);
    out["Body_Mass_Index"]        = mat_to_json(res.Body_Mass_Index);
    out["BMI_Category"]           = strmat_to_json(res.BMI_Category);
    out["Energy_Intake"]          = mat_to_json(res.Energy_Intake);
    out["Correct_Values"]         = res.Correct_Values;
    out["Model_Type"]             = res.Model_Type;
    return out;
}

// Reference-EI helper used by child_weight when EI is not supplied. Mirrors
// bw_cpp/__init__.py:child_reference_EI exactly — including the swapped FFM/FM
// argument positions (R passes (age, sex, FM, FFM) into a wrapper whose params
// are (age, sex, FFM, FM)).
static bw::Matrix child_reference_EI(const std::vector<double>& age,
                                     const std::vector<double>& sex,
                                     const std::vector<double>& FM_arg,
                                     const std::vector<double>& FFM_arg,
                                     double days, double dt) {
    std::vector<double> dummy_ei(1, 0.0);
    bw::Child P(age, sex, FFM_arg, FM_arg, dummy_ei, 1, 1, dt, /*check=*/false);
    std::size_t nind  = age.size();
    std::size_t ncols = static_cast<std::size_t>(std::floor(days / dt) + 1);
    bw::Matrix out(nind, ncols);
    for (double i = 0; i < std::floor(days / dt) + 1; i += 1.0) {
        std::vector<double> t(nind);
        for (std::size_t k = 0; k < nind; ++k) t[k] = age[k] + dt * i / 365.0;
        std::vector<double> col = P.IntakeReference(t);
        std::size_t ci = static_cast<std::size_t>(i);
        for (std::size_t k = 0; k < nind; ++k) out(k, ci) = col[k];
    }
    // bw_cpp transposes before passing to child_classic — return the transposed form.
    bw::Matrix t(out.ncol, out.nrow);
    for (std::size_t i = 0; i < out.nrow; ++i)
        for (std::size_t k = 0; k < out.ncol; ++k)
            t(k, i) = out(i, k);
    return t;
}

static json run_child(const json& args) {
    auto getarr = [&](const char* k) -> const json* {
        auto it = args.find(k);
        return it == args.end() ? nullptr : &(*it);
    };

    std::size_t n = 1;
    {
        auto* aj = getarr("age");
        if (aj && aj->is_array()) n = aj->size();
    }
    std::vector<double> age_v = as_vec(*getarr("age"), n);
    std::vector<double> sex_v = sex_as_num(*getarr("sex"));
    while (sex_v.size() < n) sex_v.push_back(sex_v.back());

    double days = args.value("days", 365.0);
    double dt   = args.value("dt", 1.0);
    bool check  = args.value("check_values", true);

    // Default FM/FFM via reference helper (mirrors bw_cpp.child_reference_FFMandFM).
    std::vector<double> FM_v, FFM_v;
    auto* FMj  = getarr("FM");
    auto* FFMj = getarr("FFM");
    if (FMj && FFMj) {
        FM_v  = as_vec(*FMj,  n);
        FFM_v = as_vec(*FFMj, n);
    } else {
        std::vector<double> dummy_ei(1, 0.0), zeros(n, 0.0);
        bw::Child ref(age_v, sex_v, zeros, zeros, dummy_ei, 1, 1, 1.0, /*check=*/false);
        std::vector<double> fm_ref  = ref.FMReference(age_v);
        std::vector<double> ffm_ref = ref.FFMReference(age_v);
        FM_v  = FMj  ? as_vec(*FMj,  n) : fm_ref;
        FFM_v = FFMj ? as_vec(*FFMj, n) : ffm_ref;
    }

    auto* EIj = getarr("EI");
    auto* rpj = getarr("richardson_params");
    bool have_rich = false;
    if (rpj && rpj->is_object()) {
        have_rich = rpj->contains("K") && rpj->contains("Q") && rpj->contains("A") &&
                    rpj->contains("B") && rpj->contains("nu") && rpj->contains("C");
    }

    bw::ChildRK4Result R;
    if (EIj || !have_rich) {
        bw::Matrix EImat;
        if (EIj) {
            EImat = as_matrix(*EIj);
        } else {
            EImat = child_reference_EI(age_v, sex_v, FM_v, FFM_v, days, dt);
        }
        std::size_t nr = EImat.nrow, nc = EImat.ncol;
        std::vector<double> ei(nr * nc);
        for (std::size_t r = 0; r < nr; ++r)
            for (std::size_t c = 0; c < nc; ++c) ei[r * nc + c] = EImat(r, c);
        bw::Child P(age_v, sex_v, FFM_v, FM_v, ei, nr, nc, dt, check);
        R = P.rk4(days - 1);
    } else {
        const auto& rp = *rpj;
        bw::Child P(age_v, sex_v, FFM_v, FM_v,
                    rp["K"].get<double>(), rp["Q"].get<double>(), rp["A"].get<double>(),
                    rp["B"].get<double>(), rp["nu"].get<double>(), rp["C"].get<double>(),
                    dt, check);
        R = P.rk4(days - 1);
    }

    json out;
    out["Time"]           = vec_to_json(R.Time);
    out["Age"]            = child_to_json(R.Age,           R.nind, R.nsteps);
    out["Fat_Free_Mass"]  = child_to_json(R.Fat_Free_Mass, R.nind, R.nsteps);
    out["Fat_Mass"]       = child_to_json(R.Fat_Mass,      R.nind, R.nsteps);
    out["Body_Weight"]    = child_to_json(R.Body_Weight,   R.nind, R.nsteps);
    out["Correct_Values"] = R.Correct_Values;
    out["Model_Type"]     = std::string("Children");
    return out;
}

static json run_energy(const json& args) {
    bw::Matrix E = as_matrix(args.at("energy"));
    std::vector<double> tvec;
    for (const auto& t : args.at("time")) tvec.push_back(t.get<double>());
    std::string method = args.at("method").get<std::string>();

    // build_deterministic expects column-major Energy[r + c*nrow].
    std::vector<double> ecol(E.nrow * E.ncol);
    for (std::size_t r = 0; r < E.nrow; ++r)
        for (std::size_t c = 0; c < E.ncol; ++c) ecol[r + c * E.nrow] = E(r, c);

    std::size_t ncols = bw::energy::output_ncols(tvec.data(), tvec.size());
    std::vector<double> evals(E.nrow * ncols, 0.0);
    bw::energy::Method m;
    if (!bw::energy::parse_method(method.c_str(), &m))
        throw std::invalid_argument("unknown interpolation: " + method);
    bw::energy::build_deterministic(ecol.data(), E.nrow, E.ncol, tvec.data(), m, evals.data());

    bw::Matrix out_m(E.nrow, ncols);
    for (std::size_t r = 0; r < E.nrow; ++r)
        for (std::size_t c = 0; c < ncols; ++c) out_m(r, c) = evals[r + c * E.nrow];

    json out;
    out["energy"] = mat_to_json(out_m);
    return out;
}

// ---------- main ------------------------------------------------------------

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: parity_runner <cases.json> <output_dir>\n";
        return 2;
    }
    fs::path cases_path = argv[1];
    fs::path out_dir    = argv[2];
    fs::create_directories(out_dir);

    std::ifstream f(cases_path);
    if (!f) { std::cerr << "cannot open: " << cases_path << "\n"; return 2; }
    json cases = json::parse(f);

    std::size_t n_ok = 0, n_fail = 0;
    for (const auto& c : cases) {
        std::string id = c.at("id").get<std::string>();
        std::string fn = c.at("fn").get<std::string>();
        const json& args = c.at("args");

        try {
            json outputs;
            if      (fn == "adult_weight")  outputs = run_adult(args);
            else if (fn == "child_weight")  outputs = run_child(args);
            else if (fn == "EnergyBuilder") outputs = run_energy(args);
            else throw std::invalid_argument("unknown fn: " + fn);

            json record;
            record["id"]      = id;
            record["fn"]      = fn;
            record["outputs"] = outputs;

            std::ofstream o(out_dir / (id + ".json"));
            o << record.dump(2);
            std::cout << "  " << id << "\n";
            ++n_ok;
        } catch (const std::exception& e) {
            std::cerr << "  FAIL " << id << ": " << e.what() << "\n";
            ++n_fail;
        }
    }
    std::cout << n_ok << " cases ok, " << n_fail << " failed\n";
    return n_fail == 0 ? 0 : 1;
}
