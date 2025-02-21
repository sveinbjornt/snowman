#include <cmath>
#include <frame-info.h>
#include <limits>
#include <mfcc-stream.h>
#include <snowboy-options.h>
#include <vector-wrapper.h>

namespace snowboy {
	void MfccStreamOptions::Register(const std::string& prefix, OptionsItf* opts) {
		mel_filter.Register(prefix, opts);
		opts->Register(prefix, "num-cepstral-coeffs", "Number of cepstral coefficients.", &num_cepstral_coeffs);
		opts->Register(prefix, "use-energy", "If true, replace C0 with log energy.", &use_energy);
		opts->Register(prefix, "cepstral-lifter", "Cepstral lifter coefficient.", &cepstral_lifter);
	}

	MfccStream::MfccStream(const MfccStreamOptions& options) {
		m_options = options;
		field_x44 = -1;
		field_x48 = 0.0f;
		Matrix m;
		m.Resize(m_options.mel_filter.num_bins, m_options.mel_filter.num_bins);
		Vector v;
		ComputeDctMatrixTypeIII(&m);
		v.Resize(m_options.num_cepstral_coeffs);
		ComputeCepstralLifterCoeffs(m_options.cepstral_lifter, &v);
		m_dct_matrix.Resize(m_options.num_cepstral_coeffs, m_options.mel_filter.num_bins);
		m_dct_matrix.CopyFromMat(m.RowRange(0, m_options.num_cepstral_coeffs), MatrixTransposeType::kNoTrans);
		// TODO: These two could probably be a move
		m_cepstral_coeffs.Resize(m_options.num_cepstral_coeffs);
		m_cepstral_coeffs.CopyFromVec(v);
	}

	int MfccStream::Read(Matrix* mat, std::vector<FrameInfo>* info) {
		Matrix m;
		auto res = m_connectedStream->Read(&m, info);
		if ((res & 0xc2) != 0 || m.m_rows == 0) {
			mat->Resize(0, 0);
			info->clear();
			return res;
		}
		if (field_x44 == -1) {
			SubVector svec{m, 0};
			field_x44 = svec.m_size;
			InitMelFilterBank(field_x44);
			field_x48 = logf(static_cast<float>(field_x44) * 0.5f);
		}
		mat->Resize(m.m_rows, m_options.num_cepstral_coeffs);
		for (int r = 0; r < m.m_rows; r++) {
			SubVector svec{m, r};
			float energy = 0.0f;
			if (m_options.use_energy) {
				float f = svec.DotVec(svec);
				f = std::max(std::numeric_limits<float>::min(), f);
				f = logf(f) - field_x48;
				energy = f;
			}
			SubVector svec_out{*mat, r};
			// TODO: Couldn't we skip ComputeMfcc if we overwrite it in the next step ?
			ComputeMfcc(svec, &svec_out);
			if (m_options.use_energy) {
				svec_out.m_data[0] = energy;
			}
		}
		return res;
	}

	bool MfccStream::Reset() {
		m_melfilterbank.reset();
		field_x44 = -1;
		field_x48 = 0.0;
		return true;
	}

	std::string MfccStream::Name() const {
		return "MfccStream";
	}

	MfccStream::~MfccStream() {}

	void MfccStream::InitMelFilterBank(int num_fft_points) {
		auto options = m_options.mel_filter;
		options.num_fft_points = num_fft_points;
		m_melfilterbank.reset(new MelFilterBank(options));
	}

	void MfccStream::ComputeMfcc(const VectorBase& param_1, SubVector* param_2) const {
		Vector v;
		v.Resize(param_1.m_size);
		v.CopyFromVec(param_1);
		ComputePowerSpectrumReal(&v);
		Vector vout;
		m_melfilterbank->ComputeMelFilterBankEnergy(v, &vout);
		vout.ApplyFloor(std::numeric_limits<float>::min());
		vout.ApplyLog();
		param_2->AddMatVec(1.0, m_dct_matrix, MatrixTransposeType::kNoTrans, vout, 0.0);
		if (m_options.cepstral_lifter != 0.0) { // TODO: Comparing floats for equality is bad...
			param_2->MulElements(m_cepstral_coeffs);
		}
	}
} // namespace snowboy
