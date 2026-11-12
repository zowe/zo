package org.zowe.zo.ffm;

import java.util.List;

public interface ZdsService {
    List<ZdsBindings.ZDSEntry> listDataSets(String dsn) throws Exception;
    String readDataSet(String dsn, String codepage) throws Exception;
    String writeDataSet(String dsn, String data, String codepage, String etag) throws Exception;
    void createDataSet(String dsn, ZdsBindings.DSAttributes attrs) throws Exception;
    void createMember(String dsn) throws Exception;
    List<ZdsBindings.ZDSMem> listMembers(String dsn) throws Exception;
    void deleteDataSet(String dsn) throws Exception;
}
