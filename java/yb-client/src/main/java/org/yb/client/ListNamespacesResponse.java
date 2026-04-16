package org.yb.client;

import java.util.ArrayList;
import java.util.List;

import org.yb.annotations.InterfaceAudience;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.NamespaceIdentifierPB;

@InterfaceAudience.Public
public class ListNamespacesResponse extends YRpcResponse {

    private final MasterTypes.MasterErrorPB serverError;

    private List<NamespaceIdentifierPB> namespacesList;

    ListNamespacesResponse(long ellapsedMillis,
                            String uuid,
                            MasterTypes.MasterErrorPB serverError,
                            List<NamespaceIdentifierPB> namespacesList) {

        super(ellapsedMillis, uuid);
        this.serverError = serverError;
        this.namespacesList = namespacesList;
    }

    public List<NamespaceIdentifierPB> getNamespacesList() {
        return namespacesList;
    }

    public boolean hasError() {
        return serverError != null;
    }

    public String errorMessage() {
        if (serverError == null) {
            return "";
        }
        return serverError.getStatus().getMessage();
    }

     /**
     * Merges namespacesList from input ListNamespacesResponse into this namespacesList.
     * @return this instance
     */
    public ListNamespacesResponse mergeWith(ListNamespacesResponse inputResponse) {
        if (inputResponse == null) { return this; }
        List<NamespaceIdentifierPB> inputNamespacesList = inputResponse.getNamespacesList();
        if (inputNamespacesList != null && !inputNamespacesList.isEmpty()) {
        // Protobuf returns an unmodifiable list, so we need a new ArrayList.
        inputNamespacesList = new ArrayList<>(inputNamespacesList);
        if (this.namespacesList != null && !this.namespacesList.isEmpty()) {
            inputNamespacesList.addAll(this.namespacesList);
        }
        this.namespacesList = inputNamespacesList;
        }
        return this;
    }
}
