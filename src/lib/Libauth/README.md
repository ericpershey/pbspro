***This file explains about LibAuth API Interface descriptions and design...***

# pbs_auth_set_config
 - **Synopsis:** void pbs_auth_set_config(const pbs_auth_config_t *auth_config)
 - **Description:** This API sets configuration for the authentication library like logging method, where it can find required credentials etc... This API should be called first before calling any other LibAuth API.
 - **Arguments:**

	- const pbs_auth_config_t *auth_config

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to a configuration structure as shown below for the authentication library.

		```c
		typedef struct pbs_auth_config {
			/* Path to PBS_HOME directory (aka same value as PBS_HOME in pbs.conf). This must be a null-terminated string. */
			char *pbs_home_path;

			/* Path to PBS_EXEC directory (aka same value as PBS_EXEC in pbs.conf). This must be a null-terminated string. */
			char *pbs_exec_path;

			/* Name of authentication method (aka same value as PBS_AUTH_METHOD in pbs.conf). This must be a null-terminated string. */
			char *auth_method;

			/* Name of encryption method (aka same value as PBS_ENCRYPT_METHOD in pbs.conf). This must be a null-terminated string. */
			char *encrypt_method;

			/* Encryption mode (aka same value as PBS_ENCRYPT_MODE in pbs.conf) */
			int encrypt_mode;

			/*
			 * Function pointer to the logging method with the same signature as log_event from Liblog.
			 * With this, the user of the authentication library can redirect logs from the authentication
			 * library into respective log files or stderr in case no log files.
			 * If func is set to NULL then logs will be written to stderr (if available, else no logging at all).
			 */
			void (*logfunc)(int type, int objclass, int severity, const char *objname, const char *text);
		} pbs_auth_config_t;
		```

 - **Return Value:** None, void

# pbs_auth_create_ctx
 - **Synopsis:** int pbs_auth_create_ctx(void **ctx, int mode, int conn_type, char *hostname)
 - **Description:** This API creates an authentication context for a given mode and conn_type, which will be used by other LibAuth API for authentication, encrypt and decrypt data.
 - **Arguments:**

	- void **ctx

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to auth context to be created

	- int mode

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Specify which type of context to be created, should be one of AUTH_CLIENT or AUTH_SERVER.

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Use AUTH_CLIENT for client-side (aka who is initiating authentication) context

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Use AUTH_SERVER for server-side (aka who is authenticating incoming user/connection) context

		```c
		enum AUTH_ROLE {
			AUTH_ROLE_UNKNOWN = 0,
			AUTH_CLIENT,
			AUTH_SERVER,
			AUTH_ROLE_LAST
		};
		```

	- int conn_type

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Specify which type of connection is for which context to be created, should be one of AUTH_USER_CONN or AUTH_SERVICE_CONN

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Use AUTH_USER_CONN for user-oriented connection (aka like PBS client is connecting to PBS Server)


		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Use AUTH_SERVICE_CONN for service-oriented connection (aka like PBS Mom is connecting to PBS Server via PBS Comm)

		```c
		enum AUTH_CONN_TYPE {
			AUTH_USER_CONN = 0,
			AUTH_SERVICE_CONN
		};
		```

	- char *hostname

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;The null-terminated hostname of another authenticating party

 - **Return Value:** Integer

	- 0 - On Success

	- 1 - On Failure

 - **Cleanup:** A context created by this API should be destroyed by auth_free_ctx when the context is no more required

# pbs_auth_destroy_ctx
 - **Synopsis:** void pbs_auth_destroy_ctx(void *ctx)
 - **Description:** This API destroys the authentication context created by pbs_auth_create_ctx
 - **Arguments:**

	- void *ctx

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to auth context to be destroyed

 - **Return Value:** None, void

# pbs_auth_get_userinfo
 - **Synopsis:** int pbs_auth_get_userinfo(void *ctx, char **user, char **host, char **realm)
 - **Description:** Extract username and its realm, hostname of the connecting party from the given authentication context. Extracted user, host and realm values will be a null-terminated string. This API is mostly useful on authenticating server-side to get another party (aka auth client) information and the auth server might want to use this information from the auth library to match against the actual username/realm/hostname provided by the connecting party.
 - **Arguments:**

	- void *ctx

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to auth context from which information will be extracted

	- char **user

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to a buffer in which this API will write the user name

	- char **host

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to a buffer in which this API will write hostname

	- char **realm

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to a buffer in which this API will write the realm

 - **Return Value:** Integer

	- 0 - On Success

	- 1 - On Failure

 - **Cleanup:** Returned user, host, and realm should be freed using free() when no more required, as it will be allocated heap memory.

 - **Example:** This example shows what will be the value of the user, host, and realm. Let's take an example of GSS/Kerberos authentication, where auth client hostname is "xyz.abc.com", the username is "test" and in Kerberos configuration domain realm is "PBSPRO" so when this auth client authenticates to server using Kerberos authentication method, it will be authenticated as "test@PBSPRO" and this API will return user = test, host = xyz.abc.com, and realm = PBSPRO.

# pbs_auth_process_handshake_data
 - **Synopsis:** int pbs_auth_process_handshake_data(void *ctx, void *data_in, size_t len_in, void **data_out, size_t *len_out, int *is_handshake_done)
 - **Description:** Process incoming handshake data and do the handshake, and if required generate handshake data which will be sent to another party. If there is no incoming data then initiate a handshake and generate initial handshake data to be sent to the authentication server.
 - **Arguments:**

	- void *ctx

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to auth context for which handshake is happening

	- void *data_in

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Incoming handshake data to process if any. This can be NULL which indicates to initiate handshake and generate initial handshake data to be sent to the authentication server.

	- size_t len_in

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Length of incoming handshake data if any, else 0

	- void **data_out

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Outgoing handshake data to be sent to another authentication party, this can be NULL is handshake is completed and no further data needs to be sent.

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;On failure (aka return 1 by this API), data in data_out will be considered as error data/message, which will be sent to another authentication party as auth error data.

	- size_t *len_out

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Length of outgoing handshake/auth error data if any, else 0

	- int *is_handshake_done

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;To indicate whether handshake is completed or not, 0 - means handshake is not completed or 1 - means handshake is completed

 - **Return Value:** Integer

	- 0 - On Success

	- 1 - On Failure

 - **Cleanup:** Returned data_out (if any) should be freed using free() when no more required, as it will be allocated heap memory.

# pbs_auth_encrypt_data
 - **Synopsis:** int pbs_auth_encrypt_data(void *ctx, void *data_in, size_t len_in, void **data_out, size_t *len_out)
 - **Description:** Encrypt given clear text data with the given authentication context
 - **Arguments:**

	- void *ctx

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to auth context which will be used while encrypting given clear text data

	- void *data_in

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;clear text data to encrypt

	- size_t len_in

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Length of clear text data

	- void **data_out

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Encrypted data

	- size_t *len_out

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Length of encrypted data

 - **Return Value:** Integer

	- 0 - On Success

	- 1 - On Failure

 - **Cleanup:** Returned data_out should be freed using free() when no more required, as it will be allocated heap memory.

# pbs_auth_decrypt_data
 - **Synopsis:** int pbs_auth_decrypt_data(void *ctx, void *data_in, size_t len_in, void **data_out, size_t *len_out)
 - **Description:** Decrypt given encrypted data with the given authentication context
 - **Arguments:**

	- void *ctx

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Pointer to auth context which will be used while decrypting given encrypted data

	- void *data_in

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Encrypted data to decrypt

	- size_t len_in

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Length of Encrypted data

	- void **data_out

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;clear text data

	- size_t *len_out

		&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Length of clear text data

 - **Return Value:** Integer

	- 0 - On Success

	- 1 - On Failure

 - **Cleanup:** Returned data_out should be freed using free() when no more required, as it will be allocated heap memory.
