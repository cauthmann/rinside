#pragma once


class CppFunctionForRInsideServer: public Rcpp::CppFunctionBase {
	public:
		CppFunctionForRInsideServer(RInsideServer &server, uint32_t callback_id, const std::vector<int32_t> &types) : server(server), callback_id(callback_id), types(types) {
		}
		virtual ~CppFunctionForRInsideServer() {
		}
		SEXP operator()(SEXP* args) {
			// TODO: how do we get the amount of arguments passed? We should probably verify them.
			BEGIN_RCPP
			LOG("Callback %u called", callback_id);
			server.sendReply(RIS_REPLY_CALLBACK);
			server.stream.write(callback_id);
			size_t paramcount = types.size() - 1;
			for (size_t i=0;i<paramcount;i++) {
				LOG("Sending parameter %d at %p", (int) i, args[i]);
				try {
					server.sexp_to_stream(args[i], types[i+1], false);
				}
				catch (const std::exception &e) {
					LOG("Exception sending argument: %s", e.what());
					throw;
				}
			}

			LOG("Reading result from stream");
			SEXP result = server.sexp_from_stream();
			server.allowSendReply();

			LOG("Got a SEXP, returning");
			// TODO: verify result type?
			return result;
			END_RCPP
		}
	private:
		RInsideServer &server;
		uint32_t callback_id;
		const std::vector<int32_t> types;
};

// Instantiate the standard deleter. TODO: can we avoid this?
template void Rcpp::standard_delete_finalizer(CppFunctionForRInsideServer* obj);


namespace Rcpp{

    RCPP_API_CLASS(InternalFunctionForRInsideServer_Impl) {
    public:

        RCPP_GENERATE_CTOR_ASSIGN(InternalFunctionForRInsideServer_Impl)

        InternalFunctionForRInsideServer_Impl(RInsideServer &server, uint32_t callback_id, const std::vector<int32_t> &types) {
			set(XPtr<CppFunctionForRInsideServer>(new CppFunctionForRInsideServer(server, callback_id, types), false));
		}

        void update(SEXP){}
    private:

        inline void set( SEXP xp){
            Environment RCPP = Environment::Rcpp_namespace() ;
            Function intf = RCPP["internal_function"] ;
            Storage::set__( intf( xp ) ) ;
        }

    };

    typedef InternalFunctionForRInsideServer_Impl<PreserveStorage> InternalFunctionForRInsideServer ;

}
