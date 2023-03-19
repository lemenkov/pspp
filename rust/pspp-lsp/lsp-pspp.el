(add-to-list 'lsp-language-id-configuration '(pspp-mode . "pspp"))

(add-to-list 'lsp-language-id-configuration '(".*\\.sps$" . "pspp"))

(lsp-register-client (make-lsp-client
                      :new-connection (lsp-stdio-connection "pspp-lsp")
                      :activation-fn (lsp-activate-on "pspp")
                      :server-id 'pspp))
