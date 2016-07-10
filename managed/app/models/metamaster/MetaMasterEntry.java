package models.metamaster;

import java.util.List;
import java.util.UUID;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.Transactional;
import com.fasterxml.jackson.databind.JsonNode;

import play.data.validation.Constraints;
import play.libs.Json;

@Entity
public class MetaMasterEntry extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterEntry.class);

  // The instance UUID.
  @Id
  private UUID instanceUUID;

  // The field into which the 'masters' object is serialized.
  @Constraints.Required
  @Column(nullable = false)
  @DbJson
  private JsonNode mastersJson;

  public static final Find<UUID, MetaMasterEntry> find = new Find<UUID, MetaMasterEntry>(){};

  /**
   * Create or update the list of masters for the instance.
   * @param instanceUUID : UUID of the instance
   * @param masters      : List of MasterInfo objects
   */
  @Transactional
  public static synchronized void upsert(UUID instanceUUID, List<MasterInfo> masterList) {
    MetaMasterEntry metaMasterEntry = find.byId(instanceUUID);
    if (metaMasterEntry == null) {
      metaMasterEntry = new MetaMasterEntry();
      metaMasterEntry.instanceUUID = instanceUUID;
      LOG.info("Creating new MetaMaster entry for instance " + instanceUUID);
    }
    Masters masters = new Masters();
    masters.masterList = masterList;
    metaMasterEntry.mastersJson = Json.toJson(masters);
    metaMasterEntry.save();
  }

  /**
   * Get the info for a given instance.
   * @param instanceUUID : UUID of the instance
   * @return :ist of MasterInfo objects.
   */
  public static List<MasterInfo> get(UUID instanceUUID) {
    MetaMasterEntry metaMasterEntry = find.byId(instanceUUID);
    if (metaMasterEntry == null) {
      return null;
    }
    return Json.fromJson(metaMasterEntry.mastersJson, Masters.class).masterList;
  }

  /**
   * Delete the instance entry.
   * @param instanceUUID : the instance UUID
   */
  @Transactional
  public static synchronized boolean delete(UUID instanceUUID) {
    MetaMasterEntry metaMasterEntry = find.byId(instanceUUID);
    if (metaMasterEntry == null) {
      LOG.error("Instance " + instanceUUID + " not found, cannot delete entry.");
      return false;
    }
    return metaMasterEntry.delete();
  }

  public static class Masters {
    // The list of master info objects.
    public List<MasterInfo> masterList;
  }

  public static class MasterInfo {
    // The host information.
    public String host;

    // The port number.
    public int port;

    // TODO: Add other info here such as cloud type, region, subnet id, etc.

    @Override
    public String toString() {
      return host + ":" + port;
    }
  }
}
